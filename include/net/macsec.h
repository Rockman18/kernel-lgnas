#ifndef _NET_MACSEC_H
#define _NET_MACSEC_H

#include <linux/skbuff.h>

#define DEBUG_MACSEC
#ifdef DEBUG_MACSEC
#	define MACSEC_DUMP_PKT		print_hex_dump
#else
#	define MACSEC_DUMP_PKT(arg...)
#endif

#define CONFIG_INET_MACSEC_NR_REQ_CACHE		1

struct crypto_aead;

struct macsec_dev_ctx
{
	struct crypto_aead *aead;	
#define MACSEC_NFRAGS_CACHE	4
#define MACSEC_REQ_CACHE_MAX	256
	void		  *req_cache[MACSEC_REQ_CACHE_MAX];
	atomic_t	   req_cache_cnt;
	int		   req_cache_size;
	int		   req_cache_head;
	int		   req_cache_tail;
};

struct macsec_skb_cb {
	void *req_ctx;
	struct macsec_dev_ctx *ctx;
	int flags;
};

struct macsec_hdr_t {
	__be16 macsec_type;
	unsigned int flags	:8;
	unsigned int short_len	:8;
}__attribute__((packed));

struct macsec_ethhdr {
	unsigned char		h_dest[ETH_ALEN];	/* destination eth addr	*/
	unsigned char		h_source[ETH_ALEN];	/* source ether addr	*/
	struct macsec_hdr_t 	hdr;			/* Macsec Tag */
	__be32			h_pn;			/* Macsec Packet Number */
	__be16			h_proto;		/* Ethernet packet type ID field */
} __attribute__((packed));

extern void *pskb_put(struct sk_buff *skb, struct sk_buff *tail, int len);
__be16 macsec_type_trans(struct sk_buff *skb);
extern int macsec_netif_receive_skb(struct sk_buff *skb, __be16 type);
extern int macsec_init_state(struct net_device *dev);
void macsec_destroy(struct net_device *dev);

#endif
