/*
 * Functions related to mapping data to requests
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <scsi/sg.h>		/* for struct sg_iovec */

#include "blk.h"

int blk_rq_append_bio(struct request_queue *q, struct request *rq,
		      struct bio *bio)
{
	if (!rq->bio)
		blk_rq_bio_prep(q, rq, bio);
	else if (!ll_back_merge_fn(q, rq, bio))
		return -EINVAL;
	else {
		rq->biotail->bi_next = bio;
		rq->biotail = bio;

		rq->__data_len += bio->bi_size;
	}
	return 0;
}

static int __blk_rq_unmap_user(struct bio *bio)
{
	int ret = 0;

	if (bio) {
		if (bio_flagged(bio, BIO_USER_MAPPED))
			bio_unmap_user(bio);
		else
			ret = bio_uncopy_user(bio);
	}

	return ret;
}

static int __blk_rq_map_user(struct request_queue *q, struct request *rq,
			     struct rq_map_data *map_data, void __user *ubuf,
			     unsigned int len, gfp_t gfp_mask)
{
	unsigned long uaddr;
	struct bio *bio, *orig_bio;
	int reading, ret;

	reading = rq_data_dir(rq) == READ;

	/*
	 * if alignment requirement is satisfied, map in user pages for
	 * direct dma. else, set up kernel bounce buffers
	 */
	uaddr = (unsigned long) ubuf;
	if (blk_rq_aligned(q, ubuf, len) && !map_data)
		bio = bio_map_user(q, NULL, uaddr, len, reading, gfp_mask);
	else
		bio = bio_copy_user(q, map_data, uaddr, len, reading, gfp_mask);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	if (map_data && map_data->null_mapped)
		bio->bi_flags |= (1 << BIO_NULL_MAPPED);

	orig_bio = bio;
	blk_queue_bounce(q, &bio);

	/*
	 * We link the bounce buffer in and could have to traverse it
	 * later so we have to get a ref to prevent it from being freed
	 */
	bio_get(bio);

	ret = blk_rq_append_bio(q, rq, bio);
	if (!ret)
		return bio->bi_size;

	/* if it was boucned we must call the end io function */
	bio_endio(bio, 0);
	__blk_rq_unmap_user(orig_bio);
	bio_put(bio);
	return ret;
}

/**
 * blk_rq_map_user - map user data to a request, for REQ_TYPE_BLOCK_PC usage
 * @q:		request queue where request should be inserted
 * @rq:		request structure to fill
 * @map_data:   pointer to the rq_map_data holding pages (if necessary)
 * @ubuf:	the user buffer
 * @len:	length of user data
 * @gfp_mask:	memory allocation flags
 *
 * Description:
 *    Data will be mapped directly for zero copy I/O, if possible. Otherwise
 *    a kernel bounce buffer is used.
 *
 *    A matching blk_rq_unmap_user() must be issued at the end of I/O, while
 *    still in process context.
 *
 *    Note: The mapped bio may need to be bounced through blk_queue_bounce()
 *    before being submitted to the device, as pages mapped may be out of
 *    reach. It's the callers responsibility to make sure this happens. The
 *    original bio must be passed back in to blk_rq_unmap_user() for proper
 *    unmapping.
 */
int blk_rq_map_user(struct request_queue *q, struct request *rq,
		    struct rq_map_data *map_data, void __user *ubuf,
		    unsigned long len, gfp_t gfp_mask)
{
	unsigned long bytes_read = 0;
	struct bio *bio = NULL;
	int ret;

	if (len > (queue_max_hw_sectors(q) << 9))
		return -EINVAL;
	if (!len)
		return -EINVAL;

	if (!ubuf && (!map_data || !map_data->null_mapped))
		return -EINVAL;

	while (bytes_read != len) {
		unsigned long map_len, end, start;

		map_len = min_t(unsigned long, len - bytes_read, BIO_MAX_SIZE);
		end = ((unsigned long)ubuf + map_len + PAGE_SIZE - 1)
								>> PAGE_SHIFT;
		start = (unsigned long)ubuf >> PAGE_SHIFT;

		/*
		 * A bad offset could cause us to require BIO_MAX_PAGES + 1
		 * pages. If this happens we just lower the requested
		 * mapping len by a page so that we can fit
		 */
		if (end - start > BIO_MAX_PAGES)
			map_len -= PAGE_SIZE;

		ret = __blk_rq_map_user(q, rq, map_data, ubuf, map_len,
					gfp_mask);
		if (ret < 0)
			goto unmap_rq;
		if (!bio)
			bio = rq->bio;
		bytes_read += ret;
		ubuf += ret;

		if (map_data)
			map_data->offset += ret;
	}

	if (!bio_flagged(bio, BIO_USER_MAPPED))
		rq->cmd_flags |= REQ_COPY_USER;

	rq->buffer = NULL;
	return 0;
unmap_rq:
	blk_rq_unmap_user(bio);
	rq->bio = NULL;
	return ret;
}
EXPORT_SYMBOL(blk_rq_map_user);

/**
 * blk_rq_map_user_iov - map user data to a request, for REQ_TYPE_BLOCK_PC usage
 * @q:		request queue where request should be inserted
 * @rq:		request to map data to
 * @map_data:   pointer to the rq_map_data holding pages (if necessary)
 * @iov:	pointer to the iovec
 * @iov_count:	number of elements in the iovec
 * @len:	I/O byte count
 * @gfp_mask:	memory allocation flags
 *
 * Description:
 *    Data will be mapped directly for zero copy I/O, if possible. Otherwise
 *    a kernel bounce buffer is used.
 *
 *    A matching blk_rq_unmap_user() must be issued at the end of I/O, while
 *    still in process context.
 *
 *    Note: The mapped bio may need to be bounced through blk_queue_bounce()
 *    before being submitted to the device, as pages mapped may be out of
 *    reach. It's the callers responsibility to make sure this happens. The
 *    original bio must be passed back in to blk_rq_unmap_user() for proper
 *    unmapping.
 */
int blk_rq_map_user_iov(struct request_queue *q, struct request *rq,
			struct rq_map_data *map_data, struct sg_iovec *iov,
			int iov_count, unsigned int len, gfp_t gfp_mask)
{
	struct bio *bio;
	int i, read = rq_data_dir(rq) == READ;
	int unaligned = 0;

	if (!iov || iov_count <= 0)
		return -EINVAL;

	for (i = 0; i < iov_count; i++) {
		unsigned long uaddr = (unsigned long)iov[i].iov_base;

		if (uaddr & queue_dma_alignment(q)) {
			unaligned = 1;
			break;
		}
		if (!iov[i].iov_len)
			return -EINVAL;
	}

	if (unaligned || (q->dma_pad_mask & len) || map_data)
		bio = bio_copy_user_iov(q, map_data, iov, iov_count, read,
					gfp_mask);
	else
		bio = bio_map_user_iov(q, NULL, iov, iov_count, read, gfp_mask);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	if (bio->bi_size != len) {
		/*
		 * Grab an extra reference to this bio, as bio_unmap_user()
		 * expects to be able to drop it twice as it happens on the
		 * normal IO completion path
		 */
		bio_get(bio);
		bio_endio(bio, 0);
		__blk_rq_unmap_user(bio);
		return -EINVAL;
	}

	if (!bio_flagged(bio, BIO_USER_MAPPED))
		rq->cmd_flags |= REQ_COPY_USER;

	blk_queue_bounce(q, &bio);
	bio_get(bio);
	blk_rq_bio_prep(q, rq, bio);
	rq->buffer = NULL;
	return 0;
}
EXPORT_SYMBOL(blk_rq_map_user_iov);

/**
 * blk_rq_unmap_user - unmap a request with user data
 * @bio:	       start of bio list
 *
 * Description:
 *    Unmap a rq previously mapped by blk_rq_map_user(). The caller must
 *    supply the original rq->bio from the blk_rq_map_user() return, since
 *    the I/O completion may have changed rq->bio.
 */
int blk_rq_unmap_user(struct bio *bio)
{
	struct bio *mapped_bio;
	int ret = 0, ret2;

	while (bio) {
		mapped_bio = bio;
		if (unlikely(bio_flagged(bio, BIO_BOUNCED)))
			mapped_bio = bio->bi_private;

		ret2 = __blk_rq_unmap_user(mapped_bio);
		if (ret2 && !ret)
			ret = ret2;

		mapped_bio = bio;
		bio = bio->bi_next;
		bio_put(mapped_bio);
	}

	return ret;
}
EXPORT_SYMBOL(blk_rq_unmap_user);

struct blk_kern_sg_work {
	atomic_t bios_inflight;
	struct sg_table sg_table;
	struct scatterlist *src_sgl;
};

static void blk_free_kern_sg_work(struct blk_kern_sg_work *bw)
{
	sg_free_table(&bw->sg_table);
	kfree(bw);
	return;
}

static void blk_bio_map_kern_endio(struct bio *bio, int err)
{
	struct blk_kern_sg_work *bw = bio->bi_private;

	if (bw != NULL) {
		/* Decrement the bios in processing and, if zero, free */
		BUG_ON(atomic_read(&bw->bios_inflight) <= 0);
		if (atomic_dec_and_test(&bw->bios_inflight)) {
			if ((bio_data_dir(bio) == READ) && (err == 0)) {
				unsigned long flags;

				local_irq_save(flags);	/* to protect KMs */
				sg_copy(bw->src_sgl, bw->sg_table.sgl, 0, 0,
					KM_BIO_DST_IRQ, KM_BIO_SRC_IRQ);
				local_irq_restore(flags);
			}
			blk_free_kern_sg_work(bw);
		}
	}

	bio_put(bio);
	return;
}

static int blk_rq_copy_kern_sg(struct request *rq, struct scatterlist *sgl,
			       int nents, struct blk_kern_sg_work **pbw,
			       gfp_t gfp, gfp_t page_gfp)
{
	int res = 0, i;
	struct scatterlist *sg;
	struct scatterlist *new_sgl;
	int new_sgl_nents;
	size_t len = 0, to_copy;
	struct blk_kern_sg_work *bw;

	bw = kzalloc(sizeof(*bw), gfp);
	if (bw == NULL)
		goto out;

	bw->src_sgl = sgl;

	for_each_sg(sgl, sg, nents, i)
		len += sg->length;
	to_copy = len;

	new_sgl_nents = PFN_UP(len);

	res = sg_alloc_table(&bw->sg_table, new_sgl_nents, gfp);
	if (res != 0)
		goto out_free_bw;

	new_sgl = bw->sg_table.sgl;

	for_each_sg(new_sgl, sg, new_sgl_nents, i) {
		struct page *pg;

		pg = alloc_page(page_gfp);
		if (pg == NULL)
			goto err_free_new_sgl;

		sg_assign_page(sg, pg);
		sg->length = min_t(size_t, PAGE_SIZE, len);

		len -= PAGE_SIZE;
	}

	if (rq_data_dir(rq) == WRITE) {
		/*
		 * We need to limit amount of copied data to to_copy, because
		 * sgl might have the last element in sgl not marked as last in
		 * SG chaining.
		 */
		sg_copy(new_sgl, sgl, 0, to_copy,
			KM_USER0, KM_USER1);
	}

	*pbw = bw;
	/*
	 * REQ_COPY_USER name is misleading. It should be something like
	 * REQ_HAS_TAIL_SPACE_FOR_PADDING.
	 */
	rq->cmd_flags |= REQ_COPY_USER;

out:
	return res;

err_free_new_sgl:
	for_each_sg(new_sgl, sg, new_sgl_nents, i) {
		struct page *pg = sg_page(sg);
		if (pg == NULL)
			break;
		__free_page(pg);
	}
	sg_free_table(&bw->sg_table);

out_free_bw:
	kfree(bw);
	res = -ENOMEM;
	goto out;
}

static int __blk_rq_map_kern_sg(struct request *rq, struct scatterlist *sgl,
	int nents, struct blk_kern_sg_work *bw, gfp_t gfp)
{
	int res;
	struct request_queue *q = rq->q;
	int rw = rq_data_dir(rq);
	int max_nr_vecs, i;
	size_t tot_len;
	bool need_new_bio;
	struct scatterlist *sg, *prev_sg = NULL;
	struct bio *bio = NULL, *hbio = NULL, *tbio = NULL;
	int bios;

	if (unlikely((sgl == NULL) || (sgl->length == 0) || (nents <= 0))) {
		WARN_ON(1);
		res = -EINVAL;
		goto out;
	}

	/*
	 * Let's keep each bio allocation inside a single page to decrease
	 * probability of failure.
	 */
	max_nr_vecs =  min_t(size_t,
		((PAGE_SIZE - sizeof(struct bio)) / sizeof(struct bio_vec)),
		BIO_MAX_PAGES);

	need_new_bio = true;
	tot_len = 0;
	bios = 0;
	for_each_sg(sgl, sg, nents, i) {
		struct page *page = sg_page(sg);
		void *page_addr = page_address(page);
		size_t len = sg->length, l;
		size_t offset = sg->offset;

		tot_len += len;
		prev_sg = sg;

		/*
		 * Each segment must be aligned on DMA boundary and
		 * not on stack. The last one may have unaligned
		 * length as long as the total length is aligned to
		 * DMA padding alignment.
		 */
		if (i == nents - 1)
			l = 0;
		else
			l = len;
		if (((sg->offset | l) & queue_dma_alignment(q)) ||
		    (page_addr && object_is_on_stack(page_addr + sg->offset))) {
			res = -EINVAL;
			goto out_free_bios;
		}

		while (len > 0) {
			size_t bytes;
			int rc;

			if (need_new_bio) {
				bio = bio_kmalloc(gfp, max_nr_vecs);
				if (bio == NULL) {
					res = -ENOMEM;
					goto out_free_bios;
				}

				if (rw == WRITE)
					bio->bi_rw |= REQ_WRITE;

				bios++;
				bio->bi_private = bw;
				bio->bi_end_io = blk_bio_map_kern_endio;

				if (hbio == NULL)
					hbio = tbio = bio;
				else
					tbio = tbio->bi_next = bio;
			}

			bytes = min_t(size_t, len, PAGE_SIZE - offset);

			rc = bio_add_pc_page(q, bio, page, bytes, offset);
			if (rc < bytes) {
				if (unlikely(need_new_bio || (rc < 0))) {
					if (rc < 0)
						res = rc;
					else
						res = -EIO;
					goto out_free_bios;
				} else {
					need_new_bio = true;
					len -= rc;
					offset += rc;
					continue;
				}
			}

			need_new_bio = false;
			offset = 0;
			len -= bytes;
			page = nth_page(page, 1);
		}
	}

	if (hbio == NULL) {
		res = -EINVAL;
		goto out_free_bios;
	}

	/* Total length must be aligned on DMA padding alignment */
	if ((tot_len & q->dma_pad_mask) &&
	    !(rq->cmd_flags & REQ_COPY_USER)) {
		res = -EINVAL;
		goto out_free_bios;
	}

	if (bw != NULL)
		atomic_set(&bw->bios_inflight, bios);

	while (hbio != NULL) {
		bio = hbio;
		hbio = hbio->bi_next;
		bio->bi_next = NULL;

		blk_queue_bounce(q, &bio);

		res = blk_rq_append_bio(q, rq, bio);
		if (unlikely(res != 0)) {
			bio->bi_next = hbio;
			hbio = bio;
			/* We can have one or more bios bounced */
			goto out_unmap_bios;
		}
	}

	res = 0;

	rq->buffer = NULL;
out:
	return res;

out_unmap_bios:
	blk_rq_unmap_kern_sg(rq, res);

out_free_bios:
	while (hbio != NULL) {
		bio = hbio;
		hbio = hbio->bi_next;
		bio_put(bio);
	}
	goto out;
}

/**
 * blk_rq_map_kern_sg - map kernel data to a request, for REQ_TYPE_BLOCK_PC
 * @rq:		request to fill
 * @sgl:	area to map
 * @nents:	number of elements in @sgl
 * @gfp:	memory allocation flags
 *
 * Description:
 *    Data will be mapped directly if possible. Otherwise a bounce
 *    buffer will be used.
 */
int blk_rq_map_kern_sg(struct request *rq, struct scatterlist *sgl,
		       int nents, gfp_t gfp)
{
	int res;

	res = __blk_rq_map_kern_sg(rq, sgl, nents, NULL, gfp);
	if (unlikely(res != 0)) {
		struct blk_kern_sg_work *bw = NULL;

		res = blk_rq_copy_kern_sg(rq, sgl, nents, &bw,
				gfp, rq->q->bounce_gfp | gfp);
		if (unlikely(res != 0))
			goto out;

		res = __blk_rq_map_kern_sg(rq, bw->sg_table.sgl,
				bw->sg_table.nents, bw, gfp);
		if (res != 0) {
			blk_free_kern_sg_work(bw);
			goto out;
		}
	}

	rq->buffer = NULL;

out:
	return res;
}
EXPORT_SYMBOL(blk_rq_map_kern_sg);

/**
 * blk_rq_unmap_kern_sg - unmap a request with kernel sg
 * @rq:		request to unmap
 * @err:	non-zero error code
 *
 * Description:
 *    Unmap a rq previously mapped by blk_rq_map_kern_sg(). Must be called
 *    only in case of an error!
 */
void blk_rq_unmap_kern_sg(struct request *rq, int err)
{
	struct bio *bio = rq->bio;

	while (bio) {
		struct bio *b = bio;
		bio = bio->bi_next;
		b->bi_end_io(b, err);
	}
	rq->bio = NULL;

	return;
}
EXPORT_SYMBOL(blk_rq_unmap_kern_sg);

/**
 * blk_rq_map_kern - map kernel data to a request, for REQ_TYPE_BLOCK_PC usage
 * @q:		request queue where request should be inserted
 * @rq:		request to fill
 * @kbuf:	the kernel buffer
 * @len:	length of user data
 * @gfp_mask:	memory allocation flags
 *
 * Description:
 *    Data will be mapped directly if possible. Otherwise a bounce
 *    buffer is used. Can be called multple times to append multple
 *    buffers.
 */
int blk_rq_map_kern(struct request_queue *q, struct request *rq, void *kbuf,
		    unsigned int len, gfp_t gfp_mask)
{
	int reading = rq_data_dir(rq) == READ;
	int do_copy = 0;
	struct bio *bio;
	int ret;

	if (len > (queue_max_hw_sectors(q) << 9))
		return -EINVAL;
	if (!len || !kbuf)
		return -EINVAL;

	do_copy = !blk_rq_aligned(q, kbuf, len) || object_is_on_stack(kbuf);
	if (do_copy)
		bio = bio_copy_kern(q, kbuf, len, gfp_mask, reading);
	else
		bio = bio_map_kern(q, kbuf, len, gfp_mask);

	if (IS_ERR(bio))
		return PTR_ERR(bio);

	if (rq_data_dir(rq) == WRITE)
		bio->bi_rw |= REQ_WRITE;

	if (do_copy)
		rq->cmd_flags |= REQ_COPY_USER;

	ret = blk_rq_append_bio(q, rq, bio);
	if (unlikely(ret)) {
		/* request is too big */
		bio_put(bio);
		return ret;
	}

	blk_queue_bounce(q, &rq->bio);
	rq->buffer = NULL;
	return 0;
}
EXPORT_SYMBOL(blk_rq_map_kern);
