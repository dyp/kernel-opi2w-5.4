/*
 * Common define of private data for XRadio drivers
 *
 * Copyright (c) 2013
 * Xradio Technology Co., Ltd. <www.xradiotech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef XRADIO_H
#define XRADIO_H

#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <net/mac80211.h>
#include <asm/bitops.h>
#include <linux/version.h>


/*Macroses for Driver parameters.*/
#define XRWL_MAX_QUEUE_SZ    (128)
#define AC_QUEUE_NUM           4

#ifdef P2P_MULTIVIF
#define XRWL_MAX_VIFS        (3)
#else
#define XRWL_MAX_VIFS        (2)
#endif
#define XRWL_GENERIC_IF_ID   (2)
/* (XRWL_MAX_QUEUE_SZ/(XRWL_MAX_VIFS-1))*0.9 */
#define XRWL_HOST_VIF0_11N_THROTTLE   (58)
/* (XRWL_MAX_QUEUE_SZ/(XRWL_MAX_VIFS-1))*0.9 */
#define XRWL_HOST_VIF1_11N_THROTTLE   (58)
/* XRWL_HOST_VIF0_11N_THROTTLE*0.6 = 35 */
#define XRWL_HOST_VIF0_11BG_THROTTLE  (35)
/* XRWL_HOST_VIF0_11N_THROTTLE*0.6 = 35 */
#define XRWL_HOST_VIF1_11BG_THROTTLE  (35)

#if 0
#define XRWL_FW_VIF0_THROTTLE         (15)
#define XRWL_FW_VIF1_THROTTLE         (15)
#endif

#ifdef SUPPORT_HT40

#define MAX_RATES_STAGE   6

#else

#define MAX_RATES_STAGE   8

#endif

#define MAX_RATES_RETRY   15

#define IEEE80211_FCTL_WEP      0x4000
#define IEEE80211_QOS_DATAGRP   0x0080
#define WSM_KEY_MAX_IDX         20

#ifdef BH_PROC_THREAD
/*process tx in proc thread*/
#define BH_PROC_TX       1
/*process rx in proc thread*/
#define BH_PROC_RX       1
/*Dynamic priority adjust*/
#define BH_PROC_DPA      0
#else
#define BH_PROC_TX       0
#define BH_PROC_RX       0
#define BH_PROC_DPA      0
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
#include <uapi/linux/time.h>
#include <linux/timekeeping.h>
#include <linux/timekeeping32.h>

void xr_do_gettimeofday(struct timeval *tv);
void xr_get_monotonic_boottime(struct timespec *ts);

#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0)) */

#include "common.h"
#include "queue.h"
#include "wsm.h"
#include "scan.h"
#include "txrx.h"
#include "bh.h"
#include "ht.h"
#include "pm.h"
#include "fwio.h"
#ifdef CONFIG_XRADIO_TESTMODE
#include "nl80211_testmode_msg_copy.h"
#endif /*CONFIG_XRADIO_TESTMODE*/
#ifdef CONFIG_XRADIO_ETF
#include "etf.h"
#endif

/* #define ROC_DEBUG */
/* hidden ssid is only supported when separate probe resp IE
   configuration is supported */
#ifdef PROBE_RESP_EXTRA_IE
#define HIDDEN_SSID   1
#endif

#define XRADIO_MAX_CTRL_FRAME_LEN  (0x1000)

#define MAX_STA_IN_AP_MODE         (14)
#define WLAN_LINK_ID_MAX           (MAX_STA_IN_AP_MODE + 3)

#define XRADIO_MAX_STA_IN_AP_MODE   (5)
#define XRADIO_MAX_REQUEUE_ATTEMPTS (5)
#define XRADIO_LINK_ID_UNMAPPED     (15)
#define XRADIO_MAX_TID              (8)

#define XRADIO_TX_BLOCK_ACK_ENABLED_FOR_ALL_TID    (0x3F)
#define XRADIO_RX_BLOCK_ACK_ENABLED_FOR_ALL_TID    (0x3F)
#define XRADIO_RX_BLOCK_ACK_ENABLED_FOR_BE_TID \
       (XRADIO_TX_BLOCK_ACK_ENABLED_FOR_ALL_TID & 0x01)
#define XRADIO_TX_BLOCK_ACK_DISABLED_FOR_ALL_TID   (0)
#define XRADIO_RX_BLOCK_ACK_DISABLED_FOR_ALL_TID   (0)

#define XRADIO_BLOCK_ACK_CNT    (30)
#define XRADIO_BLOCK_ACK_THLD   (800)
#define XRADIO_BLOCK_ACK_HIST   (3)
#define XRADIO_BLOCK_ACK_INTERVAL	(1 * HZ / XRADIO_BLOCK_ACK_HIST)
#define XRWL_ALL_IFS           (-1)

#ifdef ROAM_OFFLOAD
#define XRADIO_SCAN_TYPE_ACTIVE 0x1000
#define XRADIO_SCAN_BAND_5G     0x2000
#endif /*ROAM_OFFLOAD*/

#define IEEE80211_FCTL_WEP      0x4000
#define IEEE80211_QOS_DATAGRP   0x0080
#ifdef CONFIG_XRADIO_TESTMODE
#define XRADIO_SCAN_MEASUREMENT_PASSIVE (0)
#define XRADIO_SCAN_MEASUREMENT_ACTIVE  (1)
#endif

#ifdef MCAST_FWDING
#define WSM_MAX_BUF		30
#endif

#define XRADIO_PLAT_DEVICE   "xradio_device"
#define XRADIO_WORKQUEUE   "xradio_wq"
#define XRADIO_SPARE_WORKQUEUE   "xradio_spare_wq"
#define WIFI_CONF_PATH    "/data/vendor/wifi/xr_wifi.conf"
#define XRADIO_HWINFO_FILE  "/data/vendor/wifi/hwinfo.bin"

extern char *drv_version;
extern char *drv_buildtime;
#define DRV_VERSION    drv_version
#define DRV_BUILDTIME  drv_buildtime

/* extern */ struct sbus_ops;
/* extern */ struct task_struct;
/* extern */ struct xradio_debug_priv;
/* extern */ struct xradio_debug_common;
/* extern */ struct firmware;

/* Please keep order */
enum xradio_join_status {
	XRADIO_JOIN_STATUS_PASSIVE = 0,
	XRADIO_JOIN_STATUS_MONITOR,
	XRADIO_JOIN_STATUS_STA,
	XRADIO_JOIN_STATUS_AP,
};

enum xradio_link_status {
	XRADIO_LINK_OFF,
	XRADIO_LINK_RESERVE,
	XRADIO_LINK_SOFT,
	XRADIO_LINK_HARD,
#if defined(CONFIG_XRADIO_USE_EXTENSIONS)
	XRADIO_LINK_RESET,
	XRADIO_LINK_RESET_REMAP,
#endif
};

enum xradio_bss_loss_status {
	XRADIO_BSS_LOSS_NONE,
	XRADIO_BSS_LOSS_CHECKING,
	XRADIO_BSS_LOSS_CONFIRMING,
	XRADIO_BSS_LOSS_CONFIRMED,
};

struct xradio_link_entry {
	unsigned long			timestamp;
	enum xradio_link_status		status;
#if defined(CONFIG_XRADIO_USE_EXTENSIONS)
	enum xradio_link_status		prev_status;
#endif
	u8				mac[ETH_ALEN];
	u8				buffered[XRADIO_MAX_TID];
	struct sk_buff_head		rx_queue;
};

#if defined(ROAM_OFFLOAD) || defined(CONFIG_XRADIO_TESTMODE)
struct xradio_testframe {
	u8 len;
	u8 *data;
};
#endif
#ifdef CONFIG_XRADIO_TESTMODE
struct advance_scan_elems {
	u8 scanMode;
	u16 duration;
};
/**
 * xradio_tsm_info - Keeps information about ongoing TSM collection
 * @ac: Access category for which metrics to be collected
 * @use_rx_roaming: Use received voice packets to compute roam delay
 * @sta_associated: Set to 1 after association
 * @sta_roamed: Set to 1 after successful roaming
 * @roam_delay: Roam delay
 * @rx_timestamp_vo: Timestamp of received voice packet
 * @txconf_timestamp_vo: Timestamp of received tx confirmation for
 * successfully transmitted VO packet
 * @sum_pkt_q_delay: Sum of packet queue delay
 * @sum_media_delay: Sum of media delay
 *
 */
struct xradio_tsm_info {
	u8 ac;
	u8 use_rx_roaming;
	u8 sta_associated;
	u8 sta_roamed;
	u16 roam_delay;
	u32 rx_timestamp_vo;
	u32 txconf_timestamp_vo;
	u32 sum_pkt_q_delay;
	u32 sum_media_delay;
};

/**
 * xradio_start_stop_tsm - To start or stop collecting TSM metrics in
 * xradio driver
 * @start: To start or stop collecting TSM metrics
 * @up: up for which metrics to be collected
 * @packetization_delay: Packetization delay for this TID
 *
 */
struct xradio_start_stop_tsm {
	u8 start;       /*1: To start, 0: To stop*/
	u8 up;
	u16 packetization_delay;
};

#endif /* CONFIG_XRADIO_TESTMODE */
struct xradio_common {
	struct xradio_debug_common	*debug;
	struct xradio_queue		tx_queue[AC_QUEUE_NUM];
	struct xradio_queue_stats	tx_queue_stats;

	struct platform_device  *plat_device;
	struct ieee80211_hw		*hw;
	/*
	 * 0 for sta or ap
	 * 1 for p2p device
	 * 2 for p2p interface
	 */
	struct mac_address		addresses[XRWL_MAX_VIFS];

	/*Will be a pointer to a list of VIFs - Dynamically allocated */
	struct ieee80211_vif		*vif_list[XRWL_MAX_VIFS];
	atomic_t			num_vifs;
	spinlock_t			vif_list_lock;
	u32				if_id_slot;
	struct device			*pdev;
	struct workqueue_struct		*workqueue;

	struct semaphore		conf_lock;

	/* some works can not push into a same workqueue, so create a
	 * spare workqueue to separate them.
	 */
	struct workqueue_struct 	*spare_workqueue;

	const struct sbus_ops		*sbus_ops;
	struct sbus_priv		*sbus_priv;
	int 			driver_ready;

	/* HW/FW type (HIF_...) */
	int				hw_type;
	int				hw_revision;
	int				fw_revision;

	/* firmware/hardware info */
	unsigned int tx_hdr_len;

	/* Radio data */
	int output_power;
	int noise;

	/* calibration, output power limit and rssi<->dBm conversation data */

	/* BBP/MAC state */
	const struct firmware		*sdd;
	struct ieee80211_rate		*rates;
	struct ieee80211_rate		*mcs_rates;
	u8 mac_addr[ETH_ALEN];
	/*TODO:COMBO: To be made per VIFF after mac80211 support */
	struct ieee80211_channel	*channel;
	int				channel_switch_in_progress;
	wait_queue_head_t		channel_switch_done;
	u8				channel_changed;
	u8				long_frame_max_tx_count;
	u8				short_frame_max_tx_count;
	/* TODO:COMBO: According to Hong aggregation will happen per VIFF.
	* Keeping in common structure for the time being. Will be moved to VIFF
	* after the mechanism is clear */
	u8				ba_tid_mask;
	int				ba_acc; /*TODO: Same as above */
	int				ba_cnt; /*TODO: Same as above */
	int				ba_cnt_rx; /*TODO: Same as above */
	int				ba_acc_rx; /*TODO: Same as above */
	int				ba_hist; /*TODO: Same as above */
	struct timer_list		ba_timer;/*TODO: Same as above */
	spinlock_t			ba_lock; /*TODO: Same as above */
	bool				ba_ena; /*TODO: Same as above */
	struct work_struct              ba_work; /*TODO: Same as above */
#ifdef CONFIG_PM
	struct xradio_pm_state		pm_state;
#endif
	/* BT status*/
	bool				is_BT_Present;
	u16                 BT_active;
	unsigned long       BT_duration;  /*timer jiffies*/
	struct timer_list   BT_timer;
	bool				is_go_thru_go_neg;
	u8				conf_listen_interval;

	/* BH */
	atomic_t			bh_rx;
	atomic_t			bh_tx;
	atomic_t			bh_term;
	atomic_t			bh_suspend;
	struct task_struct		*bh_thread;
	int				bh_error;
#ifdef BH_USE_SEMAPHORE
	struct semaphore		bh_sem;
	atomic_t			    bh_wk;
#else
	wait_queue_head_t		bh_wq;
#endif
	wait_queue_head_t		bh_evt_wq;
#ifdef BH_PROC_THREAD
	struct bh_proc      proc;
#endif

	int				buf_id_tx;	/* byte */
	int				buf_id_rx;	/* byte */
	int				wsm_rx_seq;	/* byte */
	int				wsm_tx_seq;	/* byte */
	int				hw_bufs_used;
	int				hw_bufs_used_vif[XRWL_MAX_VIFS];
	struct sk_buff			*skb_cache;
	struct sk_buff			*skb_reserved;
	int						 skb_resv_len;
	spinlock_t				 cache_lock;
	bool				powersave_enabled;
	bool				device_can_sleep;
	/* Keep xradio awake (WUP = 1) 1 second after each scan to avoid
	 * FW issue with sleeping/waking up. */
	atomic_t            recent_scan;
	atomic_t            suspend_state;
	atomic_t            suspend_lock_state;
	wait_queue_head_t		wsm_wakeup_done;
#ifdef HW_RESTART
	bool                exit_sync;
	int			hw_restart_work_running;
	bool                hw_restart;
	bool		    hw_cant_wakeup;
	struct work_struct  hw_restart_work;
#endif

	/* WSM */
	struct wsm_caps			wsm_caps;
	struct semaphore                wsm_cmd_sema;
	struct wsm_buf			wsm_cmd_buf;
	struct wsm_cmd			wsm_cmd;
	wait_queue_head_t		wsm_cmd_wq;
	wait_queue_head_t		wsm_startup_done;
	struct wsm_cbc			wsm_cbc;
	struct semaphore		tx_lock_sem;
	atomic_t			tx_lock;
	struct semaphore		dtor_lock;
	u32				pending_frame_id;
#ifdef CONFIG_XRADIO_TESTMODE
	/* Device Power Range */
	struct wsm_tx_power_range       txPowerRange[2];
	/* Advance Scan */
	struct advance_scan_elems	advanceScanElems;
	bool				enable_advance_scan;
	struct delayed_work		advance_scan_timeout;
#endif /* CONFIG_XRADIO_TESTMODE */

	/* WSM debug */
	int                 wsm_enable_wsm_dumps;
	u32                 wsm_dump_max_size;
	u32                 query_packetID;
	atomic_t            query_cnt;
	struct work_struct  query_work; /* for query packet */

	/* Scan status */
	struct xradio_scan scan;
	int scan_delay_status[XRWL_MAX_VIFS];
	unsigned long scan_delay_time[XRWL_MAX_VIFS];

	/* TX/RX */
	unsigned long		rx_timestamp;

	/* WSM events */
	spinlock_t		event_queue_lock;
	struct list_head	event_queue;
	struct work_struct	event_handler;

	/* TX rate policy cache */
	struct tx_policy_cache tx_policy_cache;
	struct work_struct tx_policy_upload_work;
	atomic_t upload_count;

	/* cryptographic engine information */

	/* bit field of glowing LEDs */
	u16 softled_state;

	/* statistics */
	struct ieee80211_low_level_stats stats;

	struct xradio_ht_info		ht_info;
	int				tx_burst_idx;

	struct ieee80211_iface_limit		if_limits1[2];
	struct ieee80211_iface_limit		if_limits2[2];
	struct ieee80211_iface_limit		if_limits3[3];
	struct ieee80211_iface_combination	if_combs[3];

	struct semaphore		wsm_oper_lock;
	struct delayed_work		rem_chan_timeout;
	atomic_t			remain_on_channel;
	int				roc_if_id;
	u64				roc_cookie;
	wait_queue_head_t		offchannel_wq;
	u16				offchannel_done;
	u16				prev_channel;
	int       if_id_selected;
	u32				key_map;
	struct wsm_add_key		keys[WSM_KEY_MAX_INDEX + 1];
#ifdef MCAST_FWDING
	struct wsm_buf		wsm_release_buf;
	u8			buf_released;
#endif
#ifdef ROAM_OFFLOAD
	u8				auto_scanning;
	u8				frame_rcvd;
	u8				num_scanchannels;
	u8				num_2g_channels;
	u8				num_5g_channels;
	struct wsm_scan_ch		scan_channels[48];
	struct sk_buff 			*beacon;
	struct sk_buff 			*beacon_bkp;
	struct xradio_testframe 	testframe;
#endif /*ROAM_OFFLOAD*/
#ifdef CONFIG_XRADIO_TESTMODE
	struct xradio_testframe test_frame;
	struct xr_tsm_stats		tsm_stats;
	struct xradio_tsm_info		tsm_info;
	spinlock_t			tsm_lock;
	struct xradio_start_stop_tsm	start_stop_tsm;
#endif /* CONFIG_XRADIO_TESTMODE */
	u8          connected_sta_cnt;
	u16			vif0_throttle;
	u16			vif1_throttle;
#ifdef	MONITOR_MODE
	int			monitor_if_id;
	bool			monitor_running;
#endif
#ifdef BOOT_NOT_READY_FIX
	u8          boot_not_ready_cnt;
	u8          boot_not_ready;
#endif
	u8          join_chan;
};

/* Virtual Interface State. One copy per VIF */
struct xradio_vif {
	atomic_t			enabled;
	spinlock_t			vif_lock;
	int				if_id;
	/*TODO: Split into Common and VIF parts */
	struct xradio_debug_priv	*debug;
	/* BBP/MAC state */
	u8 bssid[ETH_ALEN];
	struct wsm_edca_params		edca;
	struct wsm_tx_queue_params	tx_queue_params;
	struct wsm_association_mode	association_mode;
	struct wsm_set_bss_params	bss_params;
	struct wsm_set_pm		powersave_mode;
	struct wsm_set_pm		firmware_ps_mode;
	int				power_set_true;
	int				user_power_set_true;
	u8				user_pm_mode;
	int				cqm_rssi_thold;
	unsigned			cqm_rssi_hyst;
	unsigned			cqm_tx_failure_thold;
	unsigned			cqm_tx_failure_count;
	bool				cqm_use_rssi;
	int				cqm_link_loss_count;
	int				cqm_beacon_loss_count;
	int				mode;
	bool				enable_beacon;
	int				beacon_int;
	size_t				ssid_length;
	u8				ssid[IEEE80211_MAX_SSID_LEN];
#ifdef HIDDEN_SSID
	bool				hidden_ssid;
#endif
	bool				listening;
	struct wsm_rx_filter		rx_filter;
	struct wsm_beacon_filter_table	bf_table;
	struct wsm_beacon_filter_control bf_control;
	struct wsm_multicast_filter	multicast_filter;
	bool				has_multicast_subscription;
	struct wsm_broadcast_addr_filter	broadcast_filter;
	bool				disable_beacon_filter;
	struct wsm_arp_ipv4_filter      filter4;
#ifdef IPV6_FILTERING
	struct wsm_ndp_ipv6_filter 	filter6;
#endif /*IPV6_FILTERING*/
	struct work_struct		update_filtering_work;
	struct work_struct		set_beacon_wakeup_period_work;
#ifdef CONFIG_PM
	struct xradio_pm_state_vif	pm_state_vif;
#endif
	/*TODO: Add support in mac80211 for psmode info per VIF */
	struct wsm_p2p_ps_modeinfo	p2p_ps_modeinfo;
	struct wsm_uapsd_info		uapsd_info;
	bool				setbssparams_done;
	u32				listen_interval;
	u32				erp_info;
	bool				powersave_enabled;

	/* WSM Join */
	enum xradio_join_status	join_status;
	u8			join_bssid[ETH_ALEN];
	struct work_struct	join_work;
	struct delayed_work	join_timeout;
	struct work_struct	unjoin_work;
	struct delayed_work	unjoin_delayed_work;
	struct work_struct	offchannel_work;
	int			join_dtim_period;
	atomic_t	delayed_unjoin;

	/* Security */
	s8			wep_default_key_id;
	struct work_struct	wep_key_work;
	unsigned long           rx_timestamp;
	u32                     unicast_cipher_type;


	/* AP powersave */
	u32			link_id_map;
	u32			max_sta_ap_mode;
	u32			link_id_after_dtim;
	u32			link_id_uapsd;
	u32			link_id_max;
	u32			wsm_key_max_idx;
	struct xradio_link_entry link_id_db[MAX_STA_IN_AP_MODE];
	struct work_struct	link_id_work;
	struct delayed_work	link_id_gc_work;
	u32			sta_asleep_mask;
	u32			pspoll_mask;
	bool			aid0_bit_set;
	spinlock_t		ps_state_lock;
	bool			buffered_multicasts;
	bool			tx_multicast;
	u8     last_tim[8];  /*for softap dtim*/
	struct work_struct	set_tim_work;
	struct delayed_work	set_cts_work;
	struct work_struct	multicast_start_work;
	struct work_struct	multicast_stop_work;
	struct timer_list	mcast_timeout;

	/* CQM Implementation */
	struct delayed_work	bss_loss_work;
	struct delayed_work	connection_loss_work;
	struct work_struct	tx_failure_work;
	int			delayed_link_loss;
	spinlock_t		bss_loss_lock;
	int			bss_loss_status;
	int			bss_loss_confirm_id;

	struct ieee80211_vif	*vif;
	struct xradio_common	*hw_priv;
	struct ieee80211_hw	*hw;

	/* ROC implementation */
	struct delayed_work		pending_offchanneltx_work;
#if defined(CONFIG_XRADIO_USE_EXTENSIONS)
	/* Workaround for WFD testcase 6.1.10*/
	struct work_struct	linkid_reset_work;
	u8			action_frame_sa[ETH_ALEN];
	u8			action_linkid;
#endif
	/* Some optimizations for tx rate build.*/
	u32          base_rates;
	u32          oper_rates;

	bool			htcap;
#ifdef AP_HT_CAP_UPDATE
	u16                     ht_info;
	struct work_struct      ht_info_update_work;
#endif

#ifdef AP_HT_COMPAT_FIX
	u16    ht_compat_cnt;
	u16    ht_compat_det;
#endif

#ifdef AP_ARP_COMPAT_FIX
	u16    arp_compat_cnt;
#endif
	bool	is_mfp_connect;
};
struct xradio_sta_priv {
	int link_id;
	struct xradio_vif *priv;
};
enum xradio_data_filterid {
	IPV4ADDR_FILTER_ID = 0,
#ifdef IPV6_FILTERING
	IPV6ADDR_FILTER_ID,
#endif /*IPV6_FILTERING*/
};

#ifdef IPV6_FILTERING
/* IPV6 host addr info */
struct ipv6_addr_info {
	u8 filter_mode;
	u8 address_mode;
	u16 ipv6[8];
};
#endif /*IPV6_FILTERING*/

/* Datastructure for LLC-SNAP HDR */
#define P80211_OUI_LEN  3
struct ieee80211_snap_hdr {
	u8    dsap;   /* always 0xAA */
	u8    ssap;   /* always 0xAA */
	u8    ctrl;   /* always 0x03 */
	u8    oui[P80211_OUI_LEN];    /* organizational universal id */
} __packed;


#ifdef TES_P2P_0002_ROC_RESTART
extern s32  TES_P2P_0002_roc_dur;
extern s32  TES_P2P_0002_roc_sec;
extern s32  TES_P2P_0002_roc_usec;
extern u32  TES_P2P_0002_packet_id;
extern u32  TES_P2P_0002_state;

#define TES_P2P_0002_STATE_IDLE       0x00
#define TES_P2P_0002_STATE_SEND_RESP  0x01
#define TES_P2P_0002_STATE_GET_PKTID  0x02
#endif

/* debug.h must be here because refer to struct xradio_vif and
   struct xradio_common.*/
#include "debug.h"

/*******************************************************
 interfaces for operations of vif.
********************************************************/
static inline
struct xradio_common *xrwl_vifpriv_to_hwpriv(struct xradio_vif *priv)
{
	return priv->hw_priv;
}

static inline
struct xradio_vif *xrwl_get_vif_from_ieee80211(struct ieee80211_vif *vif)
{
	return  (struct xradio_vif *)vif->drv_priv;
}

static inline
struct xradio_vif *xrwl_hwpriv_to_vifpriv(struct xradio_common *hw_priv,
						int if_id)
{
	struct xradio_vif *vif;

	if (SYS_WARN((-1 == if_id) || (if_id > XRWL_MAX_VIFS)))
		return NULL;
	/* TODO:COMBO: During scanning frames can be received
	 * on interface ID 3 */
	spin_lock(&hw_priv->vif_list_lock);
	if (!hw_priv->vif_list[if_id]) {
		spin_unlock(&hw_priv->vif_list_lock);
		return NULL;
	}

	vif = xrwl_get_vif_from_ieee80211(hw_priv->vif_list[if_id]);
	SYS_WARN(!vif);
	if (vif && atomic_read(&vif->enabled))
		spin_lock(&vif->vif_lock);
	else
		vif = NULL;
	spin_unlock(&hw_priv->vif_list_lock);
	return vif;
}

static inline
struct xradio_vif *__xrwl_hwpriv_to_vifpriv(struct xradio_common *hw_priv,
					      int if_id)
{
	SYS_WARN((-1 == if_id) || (if_id > XRWL_MAX_VIFS));
	/* TODO:COMBO: During scanning frames can be received
	 * on interface ID 3 */
	if (!hw_priv->vif_list[if_id]) {
		return NULL;
	}

	return xrwl_get_vif_from_ieee80211(hw_priv->vif_list[if_id]);
}

static inline
struct xradio_vif *xrwl_get_activevif(struct xradio_common *hw_priv)
{
	return xrwl_hwpriv_to_vifpriv(hw_priv, ffs(hw_priv->if_id_slot)-1);
}

static inline bool is_hardware_xradio(struct xradio_common *hw_priv)
{
	return (hw_priv->hw_revision == XR829_HW_REV0);
}

static inline int xrwl_get_nr_hw_ifaces(struct xradio_common *hw_priv)
{
	switch (hw_priv->hw_revision) {
	case XR829_HW_REV0:
	default:
		return 1;
	}
}

#define xradio_for_each_vif(_hw_priv, _priv, _i) \
	for ( \
		_i = 0; \
		(_i < XRWL_MAX_VIFS)  \
		&& ((_priv = _hw_priv->vif_list[_i] ? \
		xrwl_get_vif_from_ieee80211(_hw_priv->vif_list[_i]) : NULL), 1); \
		_i++ \
	)

/*******************************************************
 interfaces for operations of queue.
********************************************************/
static inline void xradio_tx_queues_lock(struct xradio_common *hw_priv)
{
	int i;
	for (i = 0; i < 4; ++i)
		xradio_queue_lock(&hw_priv->tx_queue[i]);
}

static inline void xradio_tx_queues_unlock(struct xradio_common *hw_priv)
{
	int i;
	for (i = 0; i < 4; ++i)
		xradio_queue_unlock(&hw_priv->tx_queue[i]);
}

/*******************************************************
 interfaces for BT.
********************************************************/
static inline u16 xradio_bt_active_bit(u8 link_type)
{
	if (link_type == BT_LINK_TPYE_INQUIRY)
		return BIT(1);
	else
		return BIT(0);
}

static inline bool xradio_bt_block_type(u8 link_type)
{
	if (link_type == BT_LINK_TPYE_INQUIRY)
		return 1;
	else
		return 0;
}

static inline bool xradio_is_bt_block(struct xradio_common *hw_priv)
{
	return (hw_priv->BT_active &
			xradio_bt_active_bit(BT_LINK_TPYE_INQUIRY));
}

#endif /* XRADIO_H */
