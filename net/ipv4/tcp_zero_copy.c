/*
 *	Support routines for TCP zero copy transmit
 *
 *	Created by Vladislav Bolkhovitin
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      version 2 as published by the Free Software Foundation.
 */

#include <linux/skbuff.h>

net_get_page_callback_t net_get_page_callback __read_mostly;
EXPORT_SYMBOL(net_get_page_callback);

net_put_page_callback_t net_put_page_callback __read_mostly;
EXPORT_SYMBOL(net_put_page_callback);

/*
 * Caller of this function must ensure that at the moment when it's called
 * there are no pages in the system with net_priv field set to non-zero
 * value. Hence, this function, as well as net_get_page() and net_put_page(),
 * don't need any protection.
 */
int net_set_get_put_page_callbacks(
	net_get_page_callback_t get_callback,
	net_put_page_callback_t put_callback)
{
	int res = 0;

	if ((net_get_page_callback != NULL) && (get_callback != NULL) &&
	    (net_get_page_callback != get_callback)) {
		res = -EBUSY;
		goto out;
	}

	if ((net_put_page_callback != NULL) && (put_callback != NULL) &&
	    (net_put_page_callback != put_callback)) {
		res = -EBUSY;
		goto out;
	}

	net_get_page_callback = get_callback;
	net_put_page_callback = put_callback;

out:
	return res;
}
EXPORT_SYMBOL(net_set_get_put_page_callbacks);
