/*
 * Copyright 2002-2005, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * utilities for mac80211
 */

#include <net/mac80211.h>
#include <linux/netdevice.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/bitmap.h>
#include <linux/crc32.h>
#include <net/net_namespace.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "rate.h"
#include "mesh.h"
#include "wme.h"
#include "led.h"
#include "wep.h"

/* privid for wiphys to determine whether they belong to us or not */
void *xrmac_wiphy_privid = &xrmac_wiphy_privid;

struct ieee80211_hw *xrmac_wiphy_to_ieee80211_hw(struct wiphy *wiphy)
{
	struct ieee80211_local *local;
	BUG_ON(!wiphy);

	local = wiphy_priv(wiphy);
	return &local->hw;
}

u8 *mac80211_get_bssid(struct ieee80211_hdr *hdr, size_t len,
			enum nl80211_iftype type)
{
	__le16 fc = hdr->frame_control;

	 /* drop ACK/CTS frames and incorrect hdr len (ctrl) */
	if (len < 16)
		return NULL;

	if (ieee80211_is_data(fc)) {
		if (len < 24) /* drop incorrect hdr len (data) */
			return NULL;

		if (ieee80211_has_a4(fc))
			return NULL;
		if (ieee80211_has_tods(fc))
			return hdr->addr1;
		if (ieee80211_has_fromds(fc))
			return hdr->addr2;

		return hdr->addr3;
	}

	if (ieee80211_is_mgmt(fc)) {
		if (len < 24) /* drop incorrect hdr len (mgmt) */
			return NULL;
		return hdr->addr3;
	}

	if (ieee80211_is_ctl(fc)) {
		if (ieee80211_is_pspoll(fc))
			return hdr->addr1;

		if (ieee80211_is_back_req(fc)) {
			switch (type) {
			case NL80211_IFTYPE_STATION:
				return hdr->addr2;
			case NL80211_IFTYPE_AP:
			case NL80211_IFTYPE_AP_VLAN:
				return hdr->addr1;
			default:
				break; /* fall through to the return */
			}
		}
	}

	return NULL;
}

void mac80211_tx_set_protected(struct ieee80211_tx_data *tx)
{
	struct sk_buff *skb = tx->skb;
	struct ieee80211_hdr *hdr;

	do {
		hdr = (struct ieee80211_hdr *) skb->data;
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	} while ((skb = skb->next));
}

int mac80211_frame_duration(enum nl80211_band band, size_t len,
			     int rate, int erp, int short_preamble)
{
	int dur;

	/* calculate duration (in microseconds, rounded up to next higher
	 * integer if it includes a fractional microsecond) to send frame of
	 * len bytes (does not include FCS) at the given rate. Duration will
	 * also include SIFS.
	 *
	 * rate is in 100 kbps, so divident is multiplied by 10 in the
	 * DIV_ROUND_UP() operations.
	 */

	if (band == NL80211_BAND_5GHZ || erp) {
		/*
		 * OFDM:
		 *
		 * N_DBPS = DATARATE x 4
		 * N_SYM = Ceiling((16+8xLENGTH+6) / N_DBPS)
		 *	(16 = SIGNAL time, 6 = tail bits)
		 * TXTIME = T_PREAMBLE + T_SIGNAL + T_SYM x N_SYM + Signal Ext
		 *
		 * T_SYM = 4 usec
		 * 802.11a - 17.5.2: aSIFSTime = 16 usec
		 * 802.11g - 19.8.4: aSIFSTime = 10 usec +
		 *	signal ext = 6 usec
		 */
		dur = 16; /* SIFS + signal ext */
		dur += 16; /* 17.3.2.3: T_PREAMBLE = 16 usec */
		dur += 4; /* 17.3.2.3: T_SIGNAL = 4 usec */
		dur += 4 * DIV_ROUND_UP((16 + 8 * (len + 4) + 6) * 10,
					4 * rate); /* T_SYM x N_SYM */
	} else {
		/*
		 * 802.11b or 802.11g with 802.11b compatibility:
		 * 18.3.4: TXTIME = PreambleLength + PLCPHeaderTime +
		 * Ceiling(((LENGTH+PBCC)x8)/DATARATE). PBCC=0.
		 *
		 * 802.11 (DS): 15.3.3, 802.11b: 18.3.4
		 * aSIFSTime = 10 usec
		 * aPreambleLength = 144 usec or 72 usec with short preamble
		 * aPLCPHeaderLength = 48 usec or 24 usec with short preamble
		 */
		dur = 10; /* aSIFSTime = 10 usec */
		dur += short_preamble ? (72 + 24) : (144 + 48);

		dur += DIV_ROUND_UP(8 * (len + 4) * 10, rate);
	}

	return dur;
}

/* Exported duration function for driver use */
__le16 mac80211_generic_frame_duration(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					enum nl80211_band band,
					size_t frame_len,
					struct ieee80211_rate *rate)
{
	struct ieee80211_sub_if_data *sdata;
	u16 dur;
	int erp;
	bool short_preamble = false;

	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
			erp = rate->flags & IEEE80211_RATE_ERP_G;
	}

	dur = mac80211_frame_duration(band, frame_len, rate->bitrate, erp,
				       short_preamble);

	return cpu_to_le16(dur);
}

__le16 mac80211_rts_duration(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif, size_t frame_len,
			      const struct ieee80211_tx_info *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	struct ieee80211_sub_if_data *sdata;
	bool short_preamble;
	int erp;
	u16 dur;
	struct ieee80211_supported_band *sband;

	sband = local->hw.wiphy->bands[frame_txctl->band];

	short_preamble = false;

	rate = &sband->bitrates[frame_txctl->control.rts_cts_rate_idx];

	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
			erp = rate->flags & IEEE80211_RATE_ERP_G;
	}

	/* CTS duration */
	dur = mac80211_frame_duration(sband->band, 10, rate->bitrate,
				       erp, short_preamble);
	/* Data frame duration */
	dur += mac80211_frame_duration(sband->band, frame_len, rate->bitrate,
					erp, short_preamble);
	/* ACK duration */
	dur += mac80211_frame_duration(sband->band, 10, rate->bitrate,
					erp, short_preamble);

	return cpu_to_le16(dur);
}

__le16 mac80211_ctstoself_duration(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    size_t frame_len,
				    const struct ieee80211_tx_info *frame_txctl)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_rate *rate;
	struct ieee80211_sub_if_data *sdata;
	bool short_preamble;
	int erp;
	u16 dur;
	struct ieee80211_supported_band *sband;

	sband = local->hw.wiphy->bands[frame_txctl->band];

	short_preamble = false;

	rate = &sband->bitrates[frame_txctl->control.rts_cts_rate_idx];
	erp = 0;
	if (vif) {
		sdata = vif_to_sdata(vif);
		short_preamble = sdata->vif.bss_conf.use_short_preamble;
		if (sdata->flags & IEEE80211_SDATA_OPERATING_GMODE)
			erp = rate->flags & IEEE80211_RATE_ERP_G;
	}

	/* Data frame duration */
	dur = mac80211_frame_duration(sband->band, frame_len, rate->bitrate,
				       erp, short_preamble);
	if (!(frame_txctl->flags & IEEE80211_TX_CTL_NO_ACK)) {
		/* ACK duration */
		dur += mac80211_frame_duration(sband->band, 10, rate->bitrate,
						erp, short_preamble);
	}

	return cpu_to_le16(dur);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23))
static bool ieee80211_all_queues_started(struct ieee80211_hw *hw)
{
	unsigned int queue;

	for (queue = 0; queue < hw->queues; queue++)
		if (mac80211_queue_stopped(hw, queue))
			return false;
	return true;
}
#endif

void mac80211_propagate_queue_wake(struct ieee80211_local *local, int queue)
{
	struct ieee80211_sub_if_data *sdata;

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		int ac;

		if (!sdata->dev)
			continue;
#ifdef CONFIG_XRMAC_XR_ROAMING_CHANGES
		if (test_bit(SDATA_STATE_OFFCHANNEL, &sdata->state)
			    || sdata->queues_locked)
#else
		if (test_bit(SDATA_STATE_OFFCHANNEL, &sdata->state))
#endif
			continue;

		if (sdata->vif.cab_queue != IEEE80211_INVAL_HW_QUEUE &&
		    local->queue_stop_reasons[sdata->vif.cab_queue] != 0)
			continue;

		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
			int ac_queue = sdata->vif.hw_queue[ac];

			if (ac_queue == queue ||
			    (sdata->vif.cab_queue == queue &&
			     local->queue_stop_reasons[ac_queue] == 0 &&
			     skb_queue_empty(&local->pending[ac_queue])))
				netif_wake_subqueue(sdata->dev, ac);
		}
	}
}

static void __mac80211_wake_queue(struct ieee80211_hw *hw, int queue,
				   enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_wake_queue(local, queue, reason);

	if (WARN_ON(queue >= hw->queues))
		return;

	if (!test_bit(reason, &local->queue_stop_reasons[queue]))
		return;

	__clear_bit(reason, &local->queue_stop_reasons[queue]);

	if (local->queue_stop_reasons[queue] != 0)
		/* someone still has this queue stopped */
		return;

	if (skb_queue_empty(&local->pending[queue])) {
		rcu_read_lock();
		mac80211_propagate_queue_wake(local, queue);
		rcu_read_unlock();
	} else
		tasklet_schedule(&local->tx_pending_tasklet);
}

void mac80211_wake_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__mac80211_wake_queue(hw, queue, reason);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void mac80211_wake_queue(struct ieee80211_hw *hw, int queue)
{
	mac80211_wake_queue_by_reason(hw, queue,
				       IEEE80211_QUEUE_STOP_REASON_DRIVER);
}

static void __mac80211_stop_queue(struct ieee80211_hw *hw, int queue,
				   enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	trace_stop_queue(local, queue, reason);

	if (WARN_ON(queue >= hw->queues))
		return;

	if (test_bit(reason, &local->queue_stop_reasons[queue]))
		return;

	__set_bit(reason, &local->queue_stop_reasons[queue]);

	rcu_read_lock();
	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		int ac;

		if (!sdata->dev)
			continue;

		for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
			if (sdata->vif.hw_queue[ac] == queue ||
			    sdata->vif.cab_queue == queue)
				netif_stop_subqueue(sdata->dev, ac);
		}
	}
	rcu_read_unlock();
}

void mac80211_stop_queue_by_reason(struct ieee80211_hw *hw, int queue,
				    enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__mac80211_stop_queue(hw, queue, reason);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void mac80211_stop_queue(struct ieee80211_hw *hw, int queue)
{
	mac80211_stop_queue_by_reason(hw, queue,
				       IEEE80211_QUEUE_STOP_REASON_DRIVER);
}

void mac80211_add_pending_skb(struct ieee80211_local *local,
			       struct sk_buff *skb)
{
	struct ieee80211_hw *hw = &local->hw;
	unsigned long flags;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	int queue = info->hw_queue;

	if (WARN_ON(!info->control.vif)) {
		kfree_skb(skb);
		return;
	}

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	__mac80211_stop_queue(hw, queue, IEEE80211_QUEUE_STOP_REASON_SKB_ADD);
	__skb_queue_tail(&local->pending[queue], skb);
	__mac80211_wake_queue(hw, queue, IEEE80211_QUEUE_STOP_REASON_SKB_ADD);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void mac80211_add_pending_skbs_fn(struct ieee80211_local *local,
				   struct sk_buff_head *skbs,
				   void (*fn)(void *data), void *data)
{
	struct ieee80211_hw *hw = &local->hw;
	struct sk_buff *skb;
	unsigned long flags;
	int queue, i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	while ((skb = skb_dequeue(skbs))) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (WARN_ON(!info->control.vif)) {
			kfree_skb(skb);
			continue;
		}

		queue = info->hw_queue;

		__mac80211_stop_queue(hw, queue,
				IEEE80211_QUEUE_STOP_REASON_SKB_ADD);

		__skb_queue_tail(&local->pending[queue], skb);
	}

	if (fn)
		fn(data);

	for (i = 0; i < hw->queues; i++)
		__mac80211_wake_queue(hw, i,
			IEEE80211_QUEUE_STOP_REASON_SKB_ADD);
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void mac80211_add_pending_skbs(struct ieee80211_local *local,
				struct sk_buff_head *skbs)
{
	mac80211_add_pending_skbs_fn(local, skbs, NULL, NULL);
}

void mac80211_stop_queues_by_reason(struct ieee80211_hw *hw,
				    enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);

	for (i = 0; i < hw->queues; i++)
		__mac80211_stop_queue(hw, i, reason);

	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void mac80211_stop_queues(struct ieee80211_hw *hw)
{
	mac80211_stop_queues_by_reason(hw,
					IEEE80211_QUEUE_STOP_REASON_DRIVER);
}

int mac80211_queue_stopped(struct ieee80211_hw *hw, int queue)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int ret;

	if (WARN_ON(queue >= hw->queues))
		return true;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);
	ret = !!local->queue_stop_reasons[queue];
	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
	return ret;
}

void mac80211_wake_queues_by_reason(struct ieee80211_hw *hw,
				     enum queue_stop_reason reason)
{
	struct ieee80211_local *local = hw_to_local(hw);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&local->queue_stop_reason_lock, flags);

	for (i = 0; i < hw->queues; i++)
		__mac80211_wake_queue(hw, i, reason);

	spin_unlock_irqrestore(&local->queue_stop_reason_lock, flags);
}

void mac80211_wake_queues(struct ieee80211_hw *hw)
{
	mac80211_wake_queues_by_reason(hw, IEEE80211_QUEUE_STOP_REASON_DRIVER);
}

void mac80211_iterate_active_interfaces(
	struct ieee80211_hw *hw,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	mutex_lock(&local->iflist_mtx);

	list_for_each_entry(sdata, &local->interfaces, list) {
		switch (sdata->vif.type) {
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_AP_VLAN:
			continue;
		default:
			break;
		}
		if (ieee80211_sdata_running(sdata))
			iterator(data, sdata->vif.addr,
				 &sdata->vif);
	}

	mutex_unlock(&local->iflist_mtx);
}

void mac80211_iterate_active_interfaces_atomic(
	struct ieee80211_hw *hw,
	void (*iterator)(void *data, u8 *mac,
			 struct ieee80211_vif *vif),
	void *data)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct ieee80211_sub_if_data *sdata;

	rcu_read_lock();

	list_for_each_entry_rcu(sdata, &local->interfaces, list) {
		switch (sdata->vif.type) {
		case NL80211_IFTYPE_MONITOR:
		case NL80211_IFTYPE_AP_VLAN:
			continue;
		default:
			break;
		}
		if (ieee80211_sdata_running(sdata))
			iterator(data, sdata->vif.addr,
				 &sdata->vif);
	}

	rcu_read_unlock();
}

/*
 * Nothing should have been stuffed into the workqueue during
 * the suspend->resume cycle. If this WARN is seen then there
 * is a bug with either the driver suspend or something in
 * mac80211 stuffing into the workqueue which we haven't yet
 * cleared during mac80211's suspend cycle.
 */
static bool ieee80211_can_queue_work(struct ieee80211_local *local)
{
	if (WARN(local->suspended && !local->resuming,
		 "queueing ieee80211 work while going to suspend\n"))
		return false;

	return true;
}

void mac80211_queue_work(struct ieee80211_hw *hw, struct work_struct *work)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (!ieee80211_can_queue_work(local))
		return;

	queue_work(local->workqueue, work);
}

void mac80211_queue_delayed_work(struct ieee80211_hw *hw,
				  struct delayed_work *dwork,
				  unsigned long delay)
{
	struct ieee80211_local *local = hw_to_local(hw);

	if (!ieee80211_can_queue_work(local))
		return;

	queue_delayed_work(local->workqueue, dwork, delay);
}
u32 mac802_11_parse_elems_crc(u8 *start, size_t len,
			       struct ieee802_11_elems *elems,
			       u64 filter, u32 crc)
{
	size_t left = len;
	u8 *pos = start;
	bool calc_crc = filter != 0;
	DECLARE_BITMAP(seen_elems, 256);

	bitmap_zero(seen_elems, 256);
	memset(elems, 0, sizeof(*elems));
	elems->ie_start = start;
	elems->total_len = len;

	while (left >= 2) {
		u8 id, elen;
		bool elem_parse_failed;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left) {
			elems->parse_error = true;
			break;
		}

		switch (id) {
		case WLAN_EID_SSID:
		case WLAN_EID_SUPP_RATES:
		case WLAN_EID_FH_PARAMS:
		case WLAN_EID_DS_PARAMS:
		case WLAN_EID_CF_PARAMS:
		case WLAN_EID_TIM:
		case WLAN_EID_IBSS_PARAMS:
		case WLAN_EID_CHALLENGE:
		case WLAN_EID_RSN:
		case WLAN_EID_ERP_INFO:
		case WLAN_EID_EXT_SUPP_RATES:
		case WLAN_EID_HT_CAPABILITY:
		case WLAN_EID_MESH_ID:
		case WLAN_EID_MESH_CONFIG:
		case WLAN_EID_PEER_MGMT:
		case WLAN_EID_PREQ:
		case WLAN_EID_PREP:
		case WLAN_EID_PERR:
		case WLAN_EID_RANN:
		case WLAN_EID_CHANNEL_SWITCH:
		case WLAN_EID_EXT_CHANSWITCH_ANN:
		case WLAN_EID_COUNTRY:
		case WLAN_EID_PWR_CONSTRAINT:
		case WLAN_EID_TIMEOUT_INTERVAL:
			if (test_bit(id, seen_elems)) {
				elems->parse_error = true;
				left -= elen;
				pos += elen;
				continue;
			}
			break;
		}

		if (calc_crc && id < 64 && (filter & (1ULL << id)))
			crc = crc32_be(crc, pos - 2, elen + 2);

		elem_parse_failed = false;

		switch (id) {
		case WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_FH_PARAMS:
			elems->fh_params = pos;
			elems->fh_params_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			elems->ds_params = pos;
			elems->ds_params_len = elen;
			break;
		case WLAN_EID_CF_PARAMS:
			elems->cf_params = pos;
			elems->cf_params_len = elen;
			break;
		case WLAN_EID_TIM:
			if (elen >= sizeof(struct ieee80211_tim_ie)) {
				elems->tim = (void *)pos;
				elems->tim_len = elen;
			} else
				elem_parse_failed = true;
			break;
		case WLAN_EID_IBSS_PARAMS:
			elems->ibss_params = pos;
			elems->ibss_params_len = elen;
			break;
		case WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case WLAN_EID_VENDOR_SPECIFIC:
			if (elen >= 4 && pos[0] == 0x00 && pos[1] == 0x50 &&
			    pos[2] == 0xf2) {
				/* Microsoft OUI (00:50:F2) */

				if (calc_crc)
					crc = crc32_be(crc, pos - 2, elen + 2);

				if (pos[3] == 1) {
					/* OUI Type 1 - WPA IE */
					elems->wpa = pos;
					elems->wpa_len = elen;
				} else if (elen >= 5 && pos[3] == 2) {
					/* OUI Type 2 - WMM IE */
					if (pos[4] == 0) {
						elems->wmm_info = pos;
						elems->wmm_info_len = elen;
					} else if (pos[4] == 1) {
						elems->wmm_param = pos;
						elems->wmm_param_len = elen;
					}
				}
			}
			break;
		case WLAN_EID_RSN:
			elems->rsn = pos;
			elems->rsn_len = elen;
			break;
		case WLAN_EID_ERP_INFO:
			elems->erp_info = pos;
			elems->erp_info_len = elen;
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		case WLAN_EID_HT_CAPABILITY:
			if (elen >= sizeof(struct ieee80211_ht_cap))
				elems->ht_cap_elem = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_HT_INFORMATION:
			if (elen >= sizeof(struct ieee80211_ht_operation))
				elems->ht_info_elem = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_MESH_ID:
			elems->mesh_id = pos;
			elems->mesh_id_len = elen;
			break;
		case WLAN_EID_MESH_CONFIG:
			if (elen >= sizeof(struct ieee80211_meshconf_ie))
				elems->mesh_config = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_PEER_MGMT:
			elems->peering = pos;
			elems->peering_len = elen;
			break;
		case WLAN_EID_PREQ:
			elems->preq = pos;
			elems->preq_len = elen;
			break;
		case WLAN_EID_PREP:
			elems->prep = pos;
			elems->prep_len = elen;
			break;
		case WLAN_EID_PERR:
			elems->perr = pos;
			elems->perr_len = elen;
			break;
		case WLAN_EID_RANN:
			if (elen >= sizeof(struct ieee80211_rann_ie))
				elems->rann = (void *)pos;
			else
				elem_parse_failed = true;
			break;
		case WLAN_EID_CHANNEL_SWITCH:
			elems->ch_switch_elem = pos;
			elems->ch_switch_elem_len = elen;
			break;
		case WLAN_EID_QUIET:
			if (!elems->quiet_elem) {
				elems->quiet_elem = pos;
				elems->quiet_elem_len = elen;
			}
			elems->num_of_quiet_elem++;
			break;
		case WLAN_EID_COUNTRY:
			elems->country_elem = pos;
			elems->country_elem_len = elen;
			break;
		case WLAN_EID_PWR_CONSTRAINT:
			elems->pwr_constr_elem = pos;
			elems->pwr_constr_elem_len = elen;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			elems->timeout_int = pos;
			elems->timeout_int_len = elen;
			break;
		default:
			break;
		}

		if (elem_parse_failed)
			elems->parse_error = true;
		else
			set_bit(id, seen_elems);

		left -= elen;
		pos += elen;
	}

	if (left != 0)
		elems->parse_error = true;

	return crc;
}

void mac802_11_parse_elems(u8 *start, size_t len,
			    struct ieee802_11_elems *elems)
{
	mac802_11_parse_elems_crc(start, len, elems, 0, 0);
}

void mac80211_set_wmm_default(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct ieee80211_tx_queue_params qparam;
	int ac;
	bool use_11b;
	int aCWmin, aCWmax;

	if (!local->ops->conf_tx)
		return;

	if (local->hw.queues < IEEE80211_NUM_ACS)
		return;

	memset(&qparam, 0, sizeof(qparam));

	use_11b = (chan_state->conf.channel->band == NL80211_BAND_2GHZ) &&
		 !(sdata->flags & IEEE80211_SDATA_OPERATING_GMODE);

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		/* Set defaults according to 802.11-2007 Table 7-37 */
		aCWmax = 1023;
		if (use_11b)
			aCWmin = 31;
		else
			aCWmin = 15;

		switch (ac) {
		case 3: /* AC_BK */
			qparam.cw_max = aCWmax;
			qparam.cw_min = aCWmin;
			qparam.txop = 0;
			qparam.aifs = 7;
			break;
		default: /* never happens but let's not leave undefined */
		case 2: /* AC_BE */
			qparam.cw_max = aCWmax;
			qparam.cw_min = aCWmin;
			qparam.txop = 0;
			qparam.aifs = 3;
			break;
		case 1: /* AC_VI */
			qparam.cw_max = aCWmin;
			qparam.cw_min = (aCWmin + 1) / 2 - 1;
			if (use_11b)
				qparam.txop = 6016/32;
			else
				qparam.txop = 3008/32;
			qparam.aifs = 2;
			break;
		case 0: /* AC_VO */
			qparam.cw_max = (aCWmin + 1) / 2 - 1;
			qparam.cw_min = (aCWmin + 1) / 4 - 1;
			if (use_11b)
				qparam.txop = 3264/32;
			else
				qparam.txop = 1504/32;
			qparam.aifs = 2;
			break;
		}

		qparam.uapsd = false;

		sdata->tx_conf[ac] = qparam;
		drv_conf_tx(local, sdata, ac, &qparam);
	}

	/* after reinitialize QoS TX queues setting to default,
	 * disable QoS at all */

	if (sdata->vif.type != NL80211_IFTYPE_MONITOR &&
	    sdata->vif.type != NL80211_IFTYPE_P2P_DEVICE) {
		sdata->vif.bss_conf.qos =
			sdata->vif.type != NL80211_IFTYPE_STATION;
		mac80211_bss_info_change_notify(sdata, BSS_CHANGED_QOS);
	}
}

void mac80211_sta_def_wmm_params(struct ieee80211_sub_if_data *sdata,
				  const size_t supp_rates_len,
				  const u8 *supp_rates)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	int i, have_higher_than_11mbit = 0;

	/* cf. IEEE 802.11 9.2.12 */
	for (i = 0; i < supp_rates_len; i++)
		if ((supp_rates[i] & 0x7f) * 5 > 110)
			have_higher_than_11mbit = 1;

	if (chan_state->conf.channel->band == NL80211_BAND_2GHZ &&
	    have_higher_than_11mbit)
		sdata->flags |= IEEE80211_SDATA_OPERATING_GMODE;
	else
		sdata->flags &= ~IEEE80211_SDATA_OPERATING_GMODE;

	mac80211_set_wmm_default(sdata);
}

u32 mac80211_mandatory_rates(struct ieee80211_local *local,
			      enum nl80211_band band)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *bitrates;
	u32 mandatory_rates;
	enum ieee80211_rate_flags mandatory_flag;
	int i;

	sband = local->hw.wiphy->bands[band];
	if (WARN_ON(!sband))
		return 1;

	if (band == NL80211_BAND_2GHZ)
		mandatory_flag = IEEE80211_RATE_MANDATORY_B;
	else
		mandatory_flag = IEEE80211_RATE_MANDATORY_A;

	bitrates = sband->bitrates;
	mandatory_rates = 0;
	for (i = 0; i < sband->n_bitrates; i++)
		if (bitrates[i].flags & mandatory_flag)
			mandatory_rates |= BIT(i);
	return mandatory_rates;
}

void mac80211_send_auth(struct ieee80211_sub_if_data *sdata,
			 u16 transaction, u16 auth_alg,
			 u8 *extra, size_t extra_len, const u8 *bssid,
			 const u8 *key, u8 key_len, u8 key_idx)
{
	struct ieee80211_local *local = sdata->local;
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	int err;

/*
	skb = dev_alloc_skb(local->hw.extra_tx_headroom +
					sizeof(*mgmt) + 6 + extra_len);
*/
	skb = dev_alloc_skb(local->hw.extra_tx_headroom + IEEE80211_WEP_IV_LEN +
					24 + 6 + extra_len + IEEE80211_WEP_ICV_LEN);

	if (!skb)
		return;

	skb_reserve(skb, local->hw.extra_tx_headroom + IEEE80211_WEP_IV_LEN);

	mgmt = (struct ieee80211_mgmt *) skb_put(skb, 24 + 6);
	memset(mgmt, 0, 24 + 6);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_AUTH);
	memcpy(mgmt->da, bssid, ETH_ALEN);
	memcpy(mgmt->sa, sdata->vif.addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	mgmt->u.auth.auth_alg = cpu_to_le16(auth_alg);
	mgmt->u.auth.auth_transaction = cpu_to_le16(transaction);
	mgmt->u.auth.status_code = cpu_to_le16(0);
	if (extra)
		memcpy(skb_put(skb, extra_len), extra, extra_len);

	if (auth_alg == WLAN_AUTH_SHARED_KEY && transaction == 3) {
		mgmt->frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);
		err = mac80211_wep_encrypt(local, skb, key, key_len, key_idx);
		WARN_ON(err);
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;
	ieee80211_tx_skb(sdata, skb);
}

int mac80211_build_preq_ies(struct ieee80211_local *local, u8 *buffer,
			     const u8 *ie, size_t ie_len,
			     enum nl80211_band band, u32 rate_mask,
			     u8 channel)
{
	struct ieee80211_supported_band *sband;
	u8 *pos;
	size_t offset = 0, noffset;
	int supp_rates_len, i;
	u8 rates[32];
	int num_rates;
	int ext_rates_len;

	sband = local->hw.wiphy->bands[band];

	pos = buffer;

#ifdef ROAM_OFFLOAD
	if (!sband)
		goto out;
#endif /*ROAM_OFFLOAD*/

	num_rates = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((BIT(i) & rate_mask) == 0)
			continue; /* skip rate */
		rates[num_rates++] = (u8) (sband->bitrates[i].bitrate / 5);
	}

	supp_rates_len = min_t(int, num_rates, 8);

	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = supp_rates_len;
	memcpy(pos, rates, supp_rates_len);
	pos += supp_rates_len;

	/* insert "request information" if in custom IEs */
	if (ie && ie_len) {
		static const u8 before_extrates[] = {
			WLAN_EID_SSID,
			WLAN_EID_SUPP_RATES,
			WLAN_EID_REQUEST,
		};
		noffset = mac80211_ie_split(ie, ie_len,
					     before_extrates,
					     ARRAY_SIZE(before_extrates),
					     offset);
		memcpy(pos, ie + offset, noffset - offset);
		pos += noffset - offset;
		offset = noffset;
	}

	ext_rates_len = num_rates - supp_rates_len;
	if (ext_rates_len > 0) {
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = ext_rates_len;
		memcpy(pos, rates + supp_rates_len, ext_rates_len);
		pos += ext_rates_len;
	}

	if (channel && sband->band == NL80211_BAND_2GHZ) {
		*pos++ = WLAN_EID_DS_PARAMS;
		*pos++ = 1;
		*pos++ = channel;
	}

	/* insert custom IEs that go before HT */
	if (ie && ie_len) {
		static const u8 before_ht[] = {
			WLAN_EID_SSID,
			WLAN_EID_SUPP_RATES,
			WLAN_EID_REQUEST,
			WLAN_EID_EXT_SUPP_RATES,
			WLAN_EID_DS_PARAMS,
			WLAN_EID_SUPPORTED_REGULATORY_CLASSES,
		};
		noffset = mac80211_ie_split(ie, ie_len,
					     before_ht, ARRAY_SIZE(before_ht),
					     offset);
		memcpy(pos, ie + offset, noffset - offset);
		pos += noffset - offset;
		offset = noffset;
	}

	if (sband->ht_cap.ht_supported) {
		u16 cap = sband->ht_cap.cap;
		__le16 tmp;

		*pos++ = WLAN_EID_HT_CAPABILITY;
		*pos++ = sizeof(struct ieee80211_ht_cap);
		memset(pos, 0, sizeof(struct ieee80211_ht_cap));
		tmp = cpu_to_le16(cap);
		memcpy(pos, &tmp, sizeof(u16));
		pos += sizeof(u16);
		*pos++ = sband->ht_cap.ampdu_factor |
			 (sband->ht_cap.ampdu_density <<
				IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);
		memcpy(pos, &sband->ht_cap.mcs, sizeof(sband->ht_cap.mcs));
		pos += sizeof(sband->ht_cap.mcs);
		pos += 2 + 4 + 1; /* ext info, BF cap, antsel */
	}

	/*
	 * If adding more here, adjust code in main.c
	 * that calculates local->scan_ies_len.
	 */

	/* add any remaining custom IEs */
	if (ie && ie_len) {
		noffset = ie_len;
		memcpy(pos, ie + offset, noffset - offset);
		pos += noffset - offset;
	}
#ifdef ROAM_OFFLOAD
out:
#endif /*ROAM_OFFLOAD*/
	return pos - buffer;
}

struct sk_buff *mac80211_build_probe_req(struct ieee80211_sub_if_data *sdata,
					  u8 *dst, u32 ratemask,
					  const u8 *ssid, size_t ssid_len,
					  const u8 *ie, size_t ie_len,
					  bool directed)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct sk_buff *skb;
	struct ieee80211_mgmt *mgmt;
	size_t buf_len;
	u8 *buf;
	u8 chan;

	/* FIXME: come up with a proper value */
	buf = kmalloc(200 + ie_len, GFP_KERNEL);
	if (!buf)
		return NULL;

	/*
	 * Do not send DS Channel parameter for directed probe requests
	 * in order to maximize the chance that we get a response.  Some
	 * badly-behaved APs don't respond when this parameter is included.
	 */
	if (directed)
		chan = 0;
	else
		chan = ieee80211_frequency_to_channel(
			chan_state->conf.channel->center_freq);

	buf_len = mac80211_build_preq_ies(local, buf, ie, ie_len,
					   chan_state->conf.channel->band,
					   ratemask, chan);

	skb = mac80211_probereq_get(&local->hw, &sdata->vif,
				     ssid, ssid_len,
				     buf, buf_len);
	if (!skb)
		goto out;

	if (dst) {
		mgmt = (struct ieee80211_mgmt *) skb->data;
		memcpy(mgmt->da, dst, ETH_ALEN);
		memcpy(mgmt->bssid, dst, ETH_ALEN);
	}

	IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_INTFL_DONT_ENCRYPT;

 out:
	kfree(buf);

	return skb;
}

void mac80211_send_probe_req(struct ieee80211_sub_if_data *sdata, u8 *dst,
			      const u8 *ssid, size_t ssid_len,
			      const u8 *ie, size_t ie_len,
			      u32 ratemask, bool directed, bool no_cck)
{
	struct sk_buff *skb;

	skb = mac80211_build_probe_req(sdata, dst, ratemask, ssid, ssid_len,
					ie, ie_len, directed);
	if (skb) {
		if (no_cck)
			IEEE80211_SKB_CB(skb)->flags |=
				IEEE80211_TX_CTL_NO_CCK_RATE;
		ieee80211_tx_skb(sdata, skb);
	}
}

u32 mac80211_sta_get_rates(struct ieee80211_local *local,
			    struct ieee802_11_elems *elems,
			    enum nl80211_band band)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *bitrates;
	size_t num_rates;
	u32 supp_rates;
	int i, j;
	sband = local->hw.wiphy->bands[band];

	if (WARN_ON(!sband))
		return 1;

	bitrates = sband->bitrates;
	num_rates = sband->n_bitrates;
	supp_rates = 0;
	for (i = 0; i < elems->supp_rates_len +
		     elems->ext_supp_rates_len; i++) {
		u8 rate = 0;
		int own_rate;
		if (i < elems->supp_rates_len)
			rate = elems->supp_rates[i];
		else if (elems->ext_supp_rates)
			rate = elems->ext_supp_rates
				[i - elems->supp_rates_len];
		own_rate = 5 * (rate & 0x7f);
		for (j = 0; j < num_rates; j++)
			if (bitrates[j].bitrate == own_rate)
				supp_rates |= BIT(j);
	}
	return supp_rates;
}

void mac80211_stop_device(struct ieee80211_local *local)
{
	ieee80211_led_radio(local, false);
	ieee80211_mod_tpt_led_trig(local, 0, IEEE80211_TPT_LEDTRIG_FL_RADIO);
	flush_workqueue(local->workqueue);
	drv_stop(local);
}

int mac80211_reconfig(struct ieee80211_local *local)
{
	struct ieee80211_hw *hw = &local->hw;
	struct ieee80211_sub_if_data *sdata;
	struct sta_info *sta;
	int res, i;
	bool reconfig_due_to_wowlan = false;
	bool suspended = local->suspended;

#ifdef CONFIG_PM
	if (local->suspended)
		local->resuming = true;

	if (local->wowlan) {
		local->wowlan = false;
		/*
		* When the driver is resumed, first data is dropped
		* by MAC layer. To eliminate it, the local->suspended
		* should be clear.
		*/
		local->suspended = false;
		res = drv_resume(local);
		if (res < 0) {
			local->suspended = suspended;
			local->resuming = false;
			return res;
		}
		if (res == 0)
			goto wake_up;
		WARN_ON(res > 1);
		/*
		 * res is 1, which means the driver requested
		 * to go through a regular reset on wakeup.
		 */
		reconfig_due_to_wowlan = true;
	}
#endif
	/*
	 * In case of hw_restart during suspend (without wowlan),
	 * cancel restart work, as we are reconfiguring the device
	 * anyway.
	 * Note that restart_work is scheduled on a frozen workqueue,
	 * so we can't deadlock in this case.
	 */
	if (suspended && local->in_reconfig && !reconfig_due_to_wowlan)
		cancel_work_sync(&local->restart_work);

	/* setup fragmentation threshold */
	drv_set_frag_threshold(local, hw->wiphy->frag_threshold);

	/* reset coverage class */
	drv_set_coverage_class(local, hw->wiphy->coverage_class);

	/* everything else happens only if HW was up & running */
	if (!local->open_count)
		goto wake_up;

	/*
	 * Upon resume hardware can sometimes be goofy due to
	 * various platform / driver / bus issues, so restarting
	 * the device may at times not work immediately. Propagate
	 * the error.
	 */
	res = drv_start(local);
	if (res) {
		WARN(local->suspended, "Hardware became unavailable "
		     "upon resume. This could be a software issue "
		     "prior to suspend or a hardware issue.\n");
		return res;
	}

	ieee80211_led_radio(local, true);
	ieee80211_mod_tpt_led_trig(local,
				   IEEE80211_TPT_LEDTRIG_FL_RADIO, 0);

	/* add interfaces */
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (sdata->vif.type != NL80211_IFTYPE_AP_VLAN &&
		    sdata->vif.type != NL80211_IFTYPE_MONITOR &&
		    ieee80211_sdata_running(sdata))
			res = drv_add_interface(local, &sdata->vif);
	}

	/* add STAs back */
	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list) {
		if (sta->uploaded) {
			sdata = sta->sdata;
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN)
				sdata = container_of(sdata->bss,
					     struct ieee80211_sub_if_data,
					     u.ap);

			WARN_ON(drv_sta_add(local, sdata, &sta->sta));
		}
	}
	mutex_unlock(&local->sta_mtx);

	/* reconfigure tx conf */
	if (hw->queues >= IEEE80211_NUM_ACS) {
		list_for_each_entry(sdata, &local->interfaces, list) {
			if (sdata->vif.type == NL80211_IFTYPE_AP_VLAN ||
			    sdata->vif.type == NL80211_IFTYPE_MONITOR ||
			    !ieee80211_sdata_running(sdata))
				continue;

			for (i = 0; i < IEEE80211_NUM_ACS; i++)
				drv_conf_tx(local, sdata, i,
					    &sdata->tx_conf[i]);
		}
	}

	/* reconfigure hardware */
	mac80211_hw_config(local, ~0);

	list_for_each_entry(sdata, &local->interfaces, list)
		mac80211_configure_filter(sdata);

	/* Finally also reconfigure all the BSS information */
	list_for_each_entry(sdata, &local->interfaces, list) {
		u32 changed;

		if (!ieee80211_sdata_running(sdata))
			continue;

		/* common change flags for all interface types */
		changed = BSS_CHANGED_ERP_CTS_PROT |
			  BSS_CHANGED_ERP_PREAMBLE |
			  BSS_CHANGED_ERP_SLOT |
			  BSS_CHANGED_HT |
			  BSS_CHANGED_BASIC_RATES |
			  BSS_CHANGED_BEACON_INT |
			  BSS_CHANGED_BSSID |
			  BSS_CHANGED_CQM |
			  BSS_CHANGED_QOS |
			  BSS_CHANGED_PS;

		switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			changed |= BSS_CHANGED_ASSOC;
			mutex_lock(&sdata->u.mgd.mtx);
			mac80211_bss_info_change_notify(sdata, changed);
			mutex_unlock(&sdata->u.mgd.mtx);
			break;
		case NL80211_IFTYPE_ADHOC:
			changed |= BSS_CHANGED_IBSS;
			/* fall through */
		case NL80211_IFTYPE_AP:
			changed |= BSS_CHANGED_SSID;
			/* fall through */
		case NL80211_IFTYPE_MESH_POINT:
			changed |= BSS_CHANGED_BEACON |
				   BSS_CHANGED_BEACON_ENABLED;
			mac80211_bss_info_change_notify(sdata, changed);
			break;
		case NL80211_IFTYPE_WDS:
		case NL80211_IFTYPE_NAN:
			break;
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
			/* ignore virtual */
			break;
		case NL80211_IFTYPE_P2P_DEVICE:
			changed = BSS_CHANGED_IDLE;
			break;
		case NL80211_IFTYPE_OCB:
			changed |= BSS_CHANGED_OCB;
			mac80211_bss_info_change_notify(sdata, changed);
			break;
		case NL80211_IFTYPE_UNSPECIFIED:
		case NUM_NL80211_IFTYPES:
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_P2P_GO:
			WARN_ON(1);
			break;
		}
	}

	/*
	 * Clear the WLAN_STA_BLOCK_BA flag so new aggregation
	 * sessions can be established after a resume.
	 *
	 * Also tear down aggregation sessions since reconfiguring
	 * them in a hardware restart scenario is not easily done
	 * right now, and the hardware will have lost information
	 * about the sessions, but we and the AP still think they
	 * are active. This is really a workaround though.
	 */
	if (hw->flags & IEEE80211_HW_AMPDU_AGGREGATION) {
		mutex_lock(&local->sta_mtx);

		list_for_each_entry(sta, &local->sta_list, list) {
			mac80211_sta_tear_down_BA_sessions(sta, true);
			clear_sta_flag(sta, WLAN_STA_BLOCK_BA);
		}

		mutex_unlock(&local->sta_mtx);
	}

	/* add back keys */
	list_for_each_entry(sdata, &local->interfaces, list)
		if (ieee80211_sdata_running(sdata))
			mac80211_enable_keys(sdata);


	/* setup RTS threshold */
	/*
	list_for_each_entry(sdata, &local->interfaces, list)
	   drv_set_rts_threshold(local, sdata, sdata->wdev.rts_threshold);
	  */

 wake_up:
	if (local->in_reconfig) {
		local->in_reconfig = false;
		barrier();
	}

	mac80211_wake_queues_by_reason(hw,
			IEEE80211_QUEUE_STOP_REASON_SUSPEND);

	/*
	 * If this is for hw restart things are still running.
	 * We may want to change that later, however.
	 */
	if (!local->suspended)
		return 0;

#ifdef CONFIG_PM
	/* first set suspended false, then resuming */
	local->suspended = false;
	mb();
	local->resuming = false;

	list_for_each_entry(sdata, &local->interfaces, list) {
		switch (sdata->vif.type) {
		case NL80211_IFTYPE_STATION:
			mac80211_sta_restart(sdata);
			break;
		case NL80211_IFTYPE_ADHOC:
			mac80211_ibss_restart(sdata);
			break;
		case NL80211_IFTYPE_MESH_POINT:
			mac80211_mesh_restart(sdata);
			break;
		default:
			break;
		}
	}

	mod_timer(&local->sta_cleanup, jiffies + 1);

	mutex_lock(&local->sta_mtx);
	list_for_each_entry(sta, &local->sta_list, list)
		xrmac_mesh_plink_restart(sta);
	mutex_unlock(&local->sta_mtx);
#else
	WARN_ON(1);
#endif
	return 0;
}

void mac80211_resume_disconnect(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_local *local;
	struct ieee80211_key *key;

	if (WARN_ON(!vif))
		return;

	sdata = vif_to_sdata(vif);
	local = sdata->local;

	if (WARN_ON(!local->resuming))
		return;

	if (WARN_ON(vif->type != NL80211_IFTYPE_STATION))
		return;

	sdata->flags |= IEEE80211_SDATA_DISCONNECT_RESUME;

	mutex_lock(&local->key_mtx);
	list_for_each_entry(key, &sdata->key_list, list)
		key->flags |= KEY_FLAG_TAINTED;
	mutex_unlock(&local->key_mtx);
}

static int check_mgd_smps(struct ieee80211_if_managed *ifmgd,
			  enum ieee80211_smps_mode *smps_mode)
{
	if (ifmgd->associated) {
		*smps_mode = ifmgd->ap_smps;

		if (*smps_mode == IEEE80211_SMPS_AUTOMATIC) {
			if (ifmgd->powersave)
				*smps_mode = IEEE80211_SMPS_DYNAMIC;
			else
				*smps_mode = IEEE80211_SMPS_OFF;
		}

		return 1;
	}

	return 0;
}

/* must hold iflist_mtx */
void mac80211_recalc_smps(struct ieee80211_local *local)
{
	struct ieee80211_sub_if_data *sdata;
	enum ieee80211_smps_mode smps_mode = IEEE80211_SMPS_OFF;
	int count = 0;

	lockdep_assert_held(&local->iflist_mtx);

	/*
	 * This function could be improved to handle multiple
	 * interfaces better, but right now it makes any
	 * non-station interfaces force SM PS to be turned
	 * off. If there are multiple station interfaces it
	 * could also use the best possible mode, e.g. if
	 * one is in static and the other in dynamic then
	 * dynamic is ok.
	 */

	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;
		if (sdata->vif.type == NL80211_IFTYPE_P2P_DEVICE)
			continue;
		if (sdata->vif.type != NL80211_IFTYPE_STATION)
			goto set;

		count += check_mgd_smps(&sdata->u.mgd, &smps_mode);

		if (count > 1) {
			smps_mode = IEEE80211_SMPS_OFF;
			break;
		}
	}

	if (smps_mode == local->smps_mode)
		return;

 set:
	local->smps_mode = smps_mode;
	/* changed flag is auto-detected for this */
	mac80211_hw_config(local, 0);
}

static bool ieee80211_id_in_list(const u8 *ids, int n_ids, u8 id)
{
	int i;

	for (i = 0; i < n_ids; i++)
		if (ids[i] == id)
			return true;
	return false;
}

/**
 * mac80211_ie_split - split an IE buffer according to ordering
 *
 * @ies: the IE buffer
 * @ielen: the length of the IE buffer
 * @ids: an array with element IDs that are allowed before
 *	the split
 * @n_ids: the size of the element ID array
 * @offset: offset where to start splitting in the buffer
 *
 * This function splits an IE buffer by updating the @offset
 * variable to point to the location where the buffer should be
 * split.
 *
 * It assumes that the given IE buffer is well-formed, this
 * has to be guaranteed by the caller!
 *
 * It also assumes that the IEs in the buffer are ordered
 * correctly, if not the result of using this function will not
 * be ordered correctly either, i.e. it does no reordering.
 *
 * The function returns the offset where the next part of the
 * buffer starts, which may be @ielen if the entire (remainder)
 * of the buffer should be used.
 */
size_t mac80211_ie_split(const u8 *ies, size_t ielen,
			  const u8 *ids, int n_ids, size_t offset)
{
	size_t pos = offset;

	while (pos < ielen && ieee80211_id_in_list(ids, n_ids, ies[pos]))
		pos += 2 + ies[pos + 1];

	return pos;
}

size_t mac80211_ie_split_vendor(const u8 *ies, size_t ielen, size_t offset)
{
	size_t pos = offset;

	while (pos < ielen && ies[pos] != WLAN_EID_VENDOR_SPECIFIC)
		pos += 2 + ies[pos + 1];

	return pos;
}

static void _mac80211_enable_rssi_reports(struct ieee80211_sub_if_data *sdata,
					    int rssi_min_thold,
					    int rssi_max_thold)
{
	trace_api_enable_rssi_reports(sdata, rssi_min_thold, rssi_max_thold);

	if (WARN_ON(sdata->vif.type != NL80211_IFTYPE_STATION))
		return;

	/*
	 * Scale up threshold values before storing it, as the RSSI averaging
	 * algorithm uses a scaled up value as well. Change this scaling
	 * factor if the RSSI averaging algorithm changes.
	 */
	sdata->u.mgd.rssi_min_thold = rssi_min_thold*16;
	sdata->u.mgd.rssi_max_thold = rssi_max_thold*16;
}

void mac80211_enable_rssi_reports(struct ieee80211_vif *vif,
				    int rssi_min_thold,
				    int rssi_max_thold)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	WARN_ON(rssi_min_thold == rssi_max_thold ||
		rssi_min_thold > rssi_max_thold);

	_mac80211_enable_rssi_reports(sdata, rssi_min_thold,
				       rssi_max_thold);
}

void mac80211_disable_rssi_reports(struct ieee80211_vif *vif)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);

	_mac80211_enable_rssi_reports(sdata, 0, 0);
}

int mac80211_add_srates_ie(struct ieee80211_vif *vif, struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct ieee80211_supported_band *sband;
	int rate;
	u8 i, rates, *pos;

	sband = local->hw.wiphy->bands[chan_state->conf.channel->band];
	rates = sband->n_bitrates;
	if (rates > 8)
		rates = 8;

	if (skb_tailroom(skb) < rates + 2)
		return -ENOMEM;

	pos = skb_put(skb, rates + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = rates;
	for (i = 0; i < rates; i++) {
		rate = sband->bitrates[i].bitrate;
		*pos++ = (u8) (rate / 5);
	}

	return 0;
}

int mac80211_add_ext_srates_ie(struct ieee80211_vif *vif, struct sk_buff *skb)
{
	struct ieee80211_sub_if_data *sdata = vif_to_sdata(vif);
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_channel_state *chan_state = ieee80211_get_channel_state(local, sdata);
	struct ieee80211_supported_band *sband;
	int rate;
	u8 i, exrates, *pos;

	sband = local->hw.wiphy->bands[chan_state->conf.channel->band];
	exrates = sband->n_bitrates;
	if (exrates > 8)
		exrates -= 8;
	else
		exrates = 0;

	if (skb_tailroom(skb) < exrates + 2)
		return -ENOMEM;

	if (exrates) {
		pos = skb_put(skb, exrates + 2);
		*pos++ = WLAN_EID_EXT_SUPP_RATES;
		*pos++ = exrates;
		for (i = 8; i < sband->n_bitrates; i++) {
			rate = sband->bitrates[i].bitrate;
			*pos++ = (u8) (rate / 5);
		}
	}
	return 0;
}

bool ieee80211_cs_valid(const struct ieee80211_cipher_scheme *cs)
{
	return !(cs == NULL || cs->cipher == 0 ||
		 cs->hdr_len < cs->pn_len + cs->pn_off ||
		 cs->hdr_len <= cs->key_idx_off ||
		 cs->key_idx_shift > 7 ||
		 cs->key_idx_mask == 0);
}

bool ieee80211_cs_list_valid(const struct ieee80211_cipher_scheme *cs, int n)
{
	int i;

	/* Ensure we have enough iftype bitmap space for all iftype values */
	WARN_ON((NUM_NL80211_IFTYPES / 8 + 1) > sizeof(cs[0].iftype));

	for (i = 0; i < n; i++)
		if (!ieee80211_cs_valid(&cs[i]))
			return false;

	return true;
}

const struct ieee80211_cipher_scheme *
ieee80211_cs_get(struct ieee80211_local *local, u32 cipher,
		 enum nl80211_iftype iftype)
{
	const struct ieee80211_cipher_scheme *l = local->hw.cipher_schemes;
	int n = local->hw.n_cipher_schemes;
	int i;
	const struct ieee80211_cipher_scheme *cs = NULL;

	for (i = 0; i < n; i++) {
		if (l[i].cipher == cipher) {
			cs = &l[i];
			break;
		}
	}

	if (!cs || !(cs->iftype & BIT(iftype)))
		return NULL;

	return cs;
}

int ieee80211_cs_headroom(struct ieee80211_local *local,
			  struct cfg80211_crypto_settings *crypto,
			  enum nl80211_iftype iftype)
{
	const struct ieee80211_cipher_scheme *cs;
	int headroom = IEEE80211_ENCRYPT_HEADROOM;
	int i;

	for (i = 0; i < crypto->n_ciphers_pairwise; i++) {
		cs = ieee80211_cs_get(local, crypto->ciphers_pairwise[i],
				      iftype);

		if (cs && headroom < cs->hdr_len)
			headroom = cs->hdr_len;
	}

	cs = ieee80211_cs_get(local, crypto->cipher_group, iftype);
	if (cs && headroom < cs->hdr_len)
		headroom = cs->hdr_len;

	return headroom;
}
