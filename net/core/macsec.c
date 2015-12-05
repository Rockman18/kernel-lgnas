
#include <linux/err.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <net/ip.h>
#include <net/macsec.h>
#include <crypto/aead.h>
#include <crypto/authenc.h>

#define MACSEC_SKB_CB(__skb) ((struct macsec_skb_cb *)&((__skb)->cb[0]))

int create_cnt = 0;
u32 delete_cnt = 0;
u32 create_opt_cnt = 0;
int delete_opt_cnt = 0;
int create_force_cnt = 0;
int macsec_ouput = 0;
int macsec_input = 0;

static int macsec_req_ctx_size(struct crypto_aead *aead, int sg_size)
{
	unsigned int len = 0;
	len += sizeof(struct aead_request) + crypto_aead_reqsize(aead);

	len = ALIGN(len, __alignof__(struct scatterlist));
	len += sizeof(struct scatterlist) * (sg_size);

	return len;
}
	
static void *macsec_alloc_req_ctx( struct macsec_skb_cb *macsec_skb,
					struct crypto_aead *aead,
					int nfrags)
{
	void 		*ctx_data;
	unsigned int 	len;
	struct macsec_dev_ctx *ctx = macsec_skb->ctx;

#if CONFIG_INET_MACSEC_NR_REQ_CACHE > 0
	if (nfrags <= MACSEC_NFRAGS_CACHE) {
	macsec_skb->flags |= 0x01;
	if (atomic_read(&ctx->req_cache_cnt)) {
		ctx_data = ctx->req_cache[ctx->req_cache_head];
		ctx->req_cache_head = (ctx->req_cache_head + 1) %
				MACSEC_REQ_CACHE_MAX;
		atomic_dec(&ctx->req_cache_cnt);
		create_opt_cnt++;
		return ctx_data;
	}
	create_force_cnt++;
	len  = ctx->req_cache_size +
			sizeof(struct scatterlist) * MACSEC_NFRAGS_CACHE;
	ctx_data = kmalloc(len, GFP_ATOMIC);
	} else 	{
		create_cnt++;
		macsec_skb->flags &= ~0x01;
		len  = ctx->req_cache_size +
				sizeof(struct scatterlist) * nfrags;
		ctx_data = kmalloc(len, GFP_ATOMIC);
	}
#else
	len  = ctx->req_cache_size +
		sizeof(struct scatterlist) * nfrags;
	ctx_data = kmalloc(len, GFP_ATOMIC);		
#endif
	return ctx_data;
}
u32 glb_free_req_ctx_out = 0;
u32 glb_free_req_ctx_in = 0;
static void macsec_free_req_ctx(struct macsec_skb_cb *macsec_skb)
{
#if CONFIG_INET_MACSEC_NR_REQ_CACHE > 0
	struct macsec_dev_ctx *ctx = macsec_skb->ctx;
			     
	if (macsec_skb->flags & 0x01) {
		if (atomic_read(&ctx->req_cache_cnt) < MACSEC_REQ_CACHE_MAX) {
			ctx->req_cache[ctx->req_cache_tail] = macsec_skb->req_ctx;
			ctx->req_cache_tail = (ctx->req_cache_tail + 1) %
					MACSEC_REQ_CACHE_MAX;
			atomic_inc(&ctx->req_cache_cnt);
			delete_opt_cnt++;
			return;
		}
	}
#endif
	delete_cnt++;
	kfree(macsec_skb->req_ctx);
}

static inline struct scatterlist *macsec_req_sg(struct crypto_aead *aead,
					       struct aead_request *req)
{
	return (struct scatterlist *) ALIGN((unsigned long) (req + 1) +
			crypto_aead_reqsize(aead), __alignof__(struct scatterlist));
}

__be16 macsec_type_trans(struct sk_buff *skb)
{
	struct macsec_ethhdr *eth;
	eth = (struct macsec_ethhdr *)(skb->data - ETH_HLEN);
	return eth->hdr.macsec_type;
}

int macsec_init_aead(struct macsec_dev_ctx *mdata)
{
	struct crypto_aead *aead;
	int err;
	char *alg_name = "macsec(gcm)";
	char key[32] = { 0x88, 0xc5, 0x12, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x07, 0x52, 0x05, 0x20, 0x9f, 0xe8, 0x6b, 0xf8,
		0x8e, 0x7f, 0xa3, 0xaa, 0x77, 0x89, 0x58, 0xd2,
		0x50, 0x61, 0x75, 0x72, 0x81, 0x39, 0x7f, 0xcc};
	int  key_len = 32;

	aead = crypto_alloc_aead(alg_name, 0, 0);
	if (IS_ERR(aead)) {
		printk("Failed to create aead transform for macsec(gcm)\n");
		err = PTR_ERR(aead);
		goto error;
	}
	
	mdata->aead = aead;

	err = crypto_aead_setkey(aead, key, key_len);
	if (err) {
		printk("Failed to set key for macsec(gcm)\n");
		goto error;
	}

	err = crypto_aead_setauthsize(aead, 24);
	if (err) {
		printk("Failed to set authsize for macsec(gcm)\n");
		err = 0;
	}
error:
	return err;
}

static void macsec_output_done_hw(struct crypto_async_request *base, int err)
{
	int ret;
	struct sk_buff *skb = base->data;
	struct net_device *dev = skb->dev;
	const struct net_device_ops *ops = dev->netdev_ops;
	
	if (err < 0) {
		macsec_free_req_ctx(MACSEC_SKB_CB(skb));
		return;
	}
	glb_free_req_ctx_out++;
	macsec_free_req_ctx(MACSEC_SKB_CB(skb));
	ret = ops->ndo_start_xmit(skb, dev);
}

int macsec_ouput_hw(struct sk_buff *skb, struct net_device *dev)
{
	int err;
	struct macsec_dev_ctx *data;
	struct crypto_aead  *aead;
	struct aead_request *req;
	struct scatterlist *sg;
	struct scatterlist *dsg;
	struct sk_buff      *trailer;
	void *macsec_req;
	int  clen;
	int  alen;
	int  nfrags;
	
	err  = -ENOMEM;

	data  = netdev_macsec_priv(dev);
	aead = data->aead;
	alen = crypto_aead_authsize(aead);
	
	alen = 16;

	if ((err = skb_cow_data(skb, alen /* + 8 */, &trailer)) < 0)
		goto error;
	nfrags = err;
	
	MACSEC_SKB_CB(skb)->ctx = data;
	macsec_req = macsec_alloc_req_ctx(MACSEC_SKB_CB(skb), aead, nfrags * 2);
	if (!macsec_req)
		goto error;
	req = (struct  aead_request*) macsec_req;

	aead_request_set_tfm(req, aead);
	sg = macsec_req_sg(aead, req);
	dsg = sg + nfrags;
	
	/* Setup SG */
	skb_to_sgvec(skb, sg, 0, skb->len);

	clen = skb->len;
	pskb_put(skb, trailer, alen);
	skb_push(skb, 8);
	skb_to_sgvec(skb, dsg, 0, skb->len);

	MACSEC_SKB_CB(skb)->req_ctx = macsec_req;
	
	aead_request_set_callback(req, 0, macsec_output_done_hw, skb);
	aead_request_set_crypt(req, sg, dsg, clen, NULL);
	macsec_ouput++;
	err = crypto_aead_encrypt(req);

	if (err == -EINPROGRESS)
		goto error;

	if (err == -EAGAIN || err == -EBUSY) {
		macsec_free_req_ctx(MACSEC_SKB_CB(skb));
		err = NET_XMIT_DROP;
	}

error:
	return err;

}

void macsec_done_input_hw(struct crypto_async_request *base, int err)
{
	struct sk_buff *skb = base->data;
	int hlen = 22; /* ETH Header len + Macsec Secutity Tag(TCI + PN) */
	int ret;
	struct macsec_ethhdr *eth;
	
	skb_reset_mac_header(skb);
	eth = (struct macsec_ethhdr *)skb_mac_header(skb);
	skb->protocol = eth->h_proto;

	pskb_trim(skb, skb->len - 16 /* icv */);
	__skb_pull(skb, hlen);
	skb_reset_network_header(skb);
	skb->transport_header = skb->network_header;

	glb_free_req_ctx_in++;
	macsec_free_req_ctx(MACSEC_SKB_CB(skb));
	ret = macsec_netif_receive_skb(skb, skb->protocol);

}

int macsec_input_hw(struct sk_buff *skb)
{
	struct macsec_dev_ctx *data;
	struct crypto_aead  *aead;
	struct aead_request *req;
	struct scatterlist  *sg;
	struct scatterlist  *dsg;
	struct sk_buff      *trailer;
	void *macsec_req;
	int  clen;
	int  nfrags;
	int eth_len = ETH_HLEN;
	int  err = -EINVAL;
	int src_len;

	data  = netdev_macsec_priv(skb->dev);
	aead = data->aead;

	if(!aead)
		goto error;
	
	if (!pskb_may_pull(skb, eth_len))
		goto error;

	if ((err = skb_cow_data(skb, 0, &trailer)) < 0)
		goto error;
	nfrags = err;

	err = -ENOMEM;

	MACSEC_SKB_CB(skb)->ctx = data;
	macsec_req = macsec_alloc_req_ctx(MACSEC_SKB_CB(skb), aead, nfrags * 2);
	if (!macsec_req)
		goto error;
	req = (struct aead_request*) macsec_req;
	aead_request_set_tfm(req, aead);
	sg = macsec_req_sg(aead, req);
	dsg = sg + nfrags;

	/* Setup SG */
	sg_init_table(sg, nfrags);
	skb_push(skb, eth_len);
	clen = skb->len;
	skb_to_sgvec(skb, sg, 0, clen);
	src_len = clen;
	
	sg_init_table(dsg, nfrags);
	clen -= 16;
	clen -= 8;
	skb_to_sgvec(skb, dsg, 0, clen);
	MACSEC_SKB_CB(skb)->req_ctx = macsec_req;
	
	aead_request_set_callback(req, 0, macsec_done_input_hw, skb);
	aead_request_set_crypt(req, sg, dsg, src_len, NULL);

	//macsec_input++;
	err = crypto_aead_decrypt(req);

	if (err == -EINPROGRESS) {
		macsec_input++;
		goto error;
	}
	if (err == -EBUSY || err == -EAGAIN) {
		macsec_free_req_ctx(MACSEC_SKB_CB(skb));
		err = NET_XMIT_DROP;
	}

error:
	return err;
	
}

static ssize_t macsec_console_driver_write(struct file *file, const char __user *buf,
					   size_t count, loff_t * ppos)
{
	if (*buf == '1') {
		printk("Printing the delete and create stats ="
				"create_cnt = %d, delete_cnt = %d, "
				"create_opt_cnt = %d, delete_opt_cnt = %d,create_force_cnt = %d\n,"
				"glb_free_req_ctx_out = %d, glb_free_req_ctx_in = %d\n,"
				"macsec_input = %d, macsec_ouput = %d\n",
		create_cnt, delete_cnt, create_opt_cnt,
		delete_opt_cnt, create_force_cnt, glb_free_req_ctx_out, glb_free_req_ctx_in,
		macsec_input, macsec_ouput);
	}
	return 1;
}

static int macsec_console_driver_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int macsec_console_driver_release(struct inode *inode, struct file *file)
{
	return 0;
}

struct file_operations macsec_console_driver_fops = {
	.owner = THIS_MODULE,
	.open = macsec_console_driver_open,
	.release = macsec_console_driver_release,
	.write = macsec_console_driver_write,
};

#define MACSEC_CONSOLE_DRIVER_NAME	"macsec"
int macsec_console_module_init(void)
{
	struct proc_dir_entry *entry;

	entry = create_proc_entry(MACSEC_CONSOLE_DRIVER_NAME, 0, NULL);
	if (entry == NULL) {
		printk(KERN_ERR "Macsec Proc entry failed !!\n");
		return -1;
	}

	entry->proc_fops = &macsec_console_driver_fops;
	printk("Macsec proc interface Initiliazed\n");
	return 0;
}

void macsec_console_module_exit(void)
{
	remove_proc_entry(MACSEC_CONSOLE_DRIVER_NAME, NULL);
}

void macsec_destroy(struct net_device *dev)
{
	struct macsec_dev_ctx *macdata = dev->macsec_priv;

	if (!macdata)
		return;

	crypto_free_aead(macdata->aead);
	/* Delete request cache */
	while ((atomic_dec_return(&macdata->req_cache_cnt)) > 0) {
		kfree(macdata->req_cache[macdata->req_cache_head]);
		macdata->req_cache_head = (macdata->req_cache_head + 1) %
				MACSEC_REQ_CACHE_MAX;
	}
	dev->macsec_priv = NULL;
	dev->macsec_output_hw = NULL;
	dev->macsec_input_hw = NULL;

	kfree(macdata);
	printk("Macsec Session Destroyed\n");
}

int macsec_init_state(struct net_device *dev)
{
	struct macsec_dev_ctx *macdata;
	int err;

	macdata = kzalloc(sizeof(*macdata), GFP_KERNEL);
	if (macdata == NULL)
		return -ENOMEM;
	
	dev->macsec_priv = macdata;
	dev->macsec_output_hw = macsec_ouput_hw;
	dev->macsec_input_hw = macsec_input_hw;
			
	err = macsec_init_aead(macdata);
	if (err)
		goto out;
	
#if CONFIG_INET_MACSEC_NR_REQ_CACHE > 0
	atomic_set(&macdata->req_cache_cnt, 0);
	macdata->req_cache_head = 0;
	macdata->req_cache_tail = 0;
#endif
	macdata->req_cache_size = macsec_req_ctx_size(macdata->aead, 0);
	printk("Macsec Session Established\n");
out:
	return err;
	
}

static int __init macsec_init(void)
{
	int ret;
	ret =  macsec_console_module_init();
	if (ret) {
		printk("Macsec proc driver could not initiliaze\n");
		return ret;
	}
	printk("Registered Macsec Interface\n");
	return 0;
}

static void __exit macsec_fini(void)
{
	macsec_console_module_exit();
	printk("Unregistered Macsec Interface\n");
}

module_init(macsec_init);
module_exit(macsec_fini);
MODULE_LICENSE("GPL");
