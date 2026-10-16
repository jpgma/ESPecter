#include "pad_link.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_npl_os.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"

static const char *TAG = "especter";

static const ble_uuid128_t s_svc_uuid = BLE_UUID128_INIT(PAD_SVC_UUID128);
static const ble_uuid128_t s_chr_uuid = BLE_UUID128_INIT(PAD_CHR_UUID128);

enum {
    PHASE_IDLE = 0,
    PHASE_SCAN,
    PHASE_CONNECTING,
    PHASE_DISCOVER,
    PHASE_LINKED,
};

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static pad_view s_view = {
    .hat = PAD_HAT_NEUTRAL,
    .link = PAD_LINK_DOWN,
};

static int s_phase = PHASE_IDLE;
static int s_have_conn;
static uint16_t s_conn;
static int64_t s_attempt_us;
static struct ble_npl_callout s_watch;
static uint16_t s_svc_start;
static uint16_t s_svc_end;
static uint16_t s_val_handle;
static uint16_t s_cccd_handle;

void ble_store_config_init(void);

static void start_scan(void);
static void note_attempt(void);

/* Supervision de 2 s. Um telefone que sumiu cai nessa janela. */
static const struct ble_gap_conn_params s_conn_params = {
    .scan_itvl = 0x0010,
    .scan_window = 0x0010,
    .itvl_min = 24,
    .itvl_max = 40,
    .latency = 0,
    .supervision_timeout = 200,
};

/* [set_link] Grava o estado do link no mailbox. */
static void set_link(uint8_t link)
{
    portENTER_CRITICAL(&s_lock);
    s_view.link = link;
    portEXIT_CRITICAL(&s_lock);
}

/* [clear_buttons] Zera os botões e o hat no mailbox. */
static void clear_buttons(void)
{
    portENTER_CRITICAL(&s_lock);
    s_view.buttons = 0;
    s_view.hat = PAD_HAT_NEUTRAL;
    portEXIT_CRITICAL(&s_lock);
}

/* [pad_link_read] Copia o mailbox do último report. */
void pad_link_read(pad_view *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_view;
    portEXIT_CRITICAL(&s_lock);
}

/* [store_report] Grava buttons e hat no mailbox. */
static void store_report(uint16_t buttons, uint8_t hat)
{
    portENTER_CRITICAL(&s_lock);
    s_view.buttons = buttons;
    s_view.hat = hat;
    portEXIT_CRITICAL(&s_lock);
}

/* [fail_link] Encerra a conexão. Se o terminate falhar, [start_scan] recomeça. */
static void fail_link(uint16_t conn)
{
    int rc = ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        clear_buttons();
        set_link(PAD_LINK_DOWN);
        start_scan();
    }
}

/* [adv_has_service] Devolve 1 se o anúncio traz o service do Pad Emu. */
static int adv_has_service(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int i;

    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
        return 0;
    }
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) != 0) {
        return 0;
    }
    for (i = 0; i < fields.num_uuids128; i++) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &s_svc_uuid.u) == 0) {
            return 1;
        }
    }
    return 0;
}

/* [mark_linked] O notify está de pé. [watch_cb] deixa esta fase quieta. */
static void mark_linked(void)
{
    s_phase = PHASE_LINKED;
    set_link(PAD_LINK_LINKED);
}

/* [on_subscribe] Marca o link quando o CCCD aceita o notify. */
static int on_subscribe(uint16_t conn_handle, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    if (error->status != 0) {
        ESP_LOGW(TAG, "subscribe failed: %d", error->status);
        fail_link(conn_handle);
        return 0;
    }
    mark_linked();
    ESP_LOGI(TAG, "linked");
    return 0;
}

/* [on_dsc] Acha o CCCD e pede o notify do report. */
static int on_dsc(uint16_t conn_handle, const struct ble_gatt_error *error,
                  uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    uint8_t value[2];
    int rc;

    (void)chr_val_handle;
    (void)arg;
    if (error->status == 0) {
        if (ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
            s_cccd_handle = dsc->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_link(conn_handle);
        return 0;
    }
    if (s_cccd_handle == 0) {
        ESP_LOGW(TAG, "report has no cccd");
        fail_link(conn_handle);
        return 0;
    }
    value[0] = 1;
    value[1] = 0;
    rc = ble_gattc_write_flat(conn_handle, s_cccd_handle, value, sizeof(value), on_subscribe, NULL);
    if (rc != 0) {
        fail_link(conn_handle);
    }
    return 0;
}

/* [on_chr] Acha o characteristic do report e pede os descriptors. */
static int on_chr(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg)
{
    int rc;

    (void)arg;
    if (error->status == 0) {
        s_val_handle = chr->val_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_link(conn_handle);
        return 0;
    }
    if (s_val_handle == 0 || s_val_handle >= s_svc_end) {
        ESP_LOGW(TAG, "report characteristic missing");
        fail_link(conn_handle);
        return 0;
    }
    s_cccd_handle = 0;
    rc = ble_gattc_disc_all_dscs(conn_handle, s_val_handle, s_svc_end, on_dsc, NULL);
    if (rc != 0) {
        fail_link(conn_handle);
    }
    return 0;
}

/* [on_svc] Acha o service do Pad Emu e pede o characteristic do report. */
static int on_svc(uint16_t conn_handle, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *svc, void *arg)
{
    int rc;

    (void)arg;
    if (error->status == 0) {
        s_svc_start = svc->start_handle;
        s_svc_end = svc->end_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_link(conn_handle);
        return 0;
    }
    if (s_svc_start == 0) {
        ESP_LOGW(TAG, "service missing");
        fail_link(conn_handle);
        return 0;
    }
    s_val_handle = 0;
    rc = ble_gattc_disc_chrs_by_uuid(conn_handle, s_svc_start, s_svc_end, &s_chr_uuid.u, on_chr, NULL);
    if (rc != 0) {
        fail_link(conn_handle);
    }
    return 0;
}

/* [gap_event] Trata scan, conexão, queda, pairing repetido e o notify do report. */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    uint8_t own_addr_type;
    int rc;
    uint8_t buf[PAD_REPORT_BYTES];
    uint16_t buttons;
    uint8_t hat;
    ble_addr_t peer_addr;
    struct ble_gap_conn_desc desc;

    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if (s_phase != PHASE_SCAN || !adv_has_service(&event->disc)) {
            return 0;
        }
        s_phase = PHASE_CONNECTING;
        note_attempt();
        peer_addr = event->disc.addr;
        rc = ble_gap_disc_cancel();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            s_phase = PHASE_IDLE;
            set_link(PAD_LINK_DOWN);
            return 0;
        }
        rc = ble_hs_id_infer_auto(0, &own_addr_type);
        if (rc != 0) {
            s_phase = PHASE_IDLE;
            set_link(PAD_LINK_DOWN);
            return 0;
        }
        rc = ble_gap_connect(own_addr_type, &peer_addr, 10000, &s_conn_params, gap_event, NULL);
        if (rc != 0) {
            ESP_LOGW(TAG, "connect failed: %d", rc);
            start_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_phase == PHASE_SCAN) {
            start_scan();
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGW(TAG, "connect status %d", event->connect.status);
            start_scan();
            return 0;
        }
        s_conn = event->connect.conn_handle;
        s_have_conn = 1;
        s_phase = PHASE_DISCOVER;
        note_attempt();
        s_svc_start = 0;
        s_svc_end = 0;
        ESP_LOGI(TAG, "connected");
        rc = ble_gattc_disc_svc_by_uuid(s_conn, &s_svc_uuid.u, on_svc, NULL);
        if (rc != 0) {
            fail_link(s_conn);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected");
        s_have_conn = 0;
        clear_buttons();
        set_link(PAD_LINK_DOWN);
        start_scan();
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (OS_MBUF_PKTLEN(event->notify_rx.om) < PAD_REPORT_BYTES) {
            return 0;
        }
        if (os_mbuf_copydata(event->notify_rx.om, 0, PAD_REPORT_BYTES, buf) != 0) {
            return 0;
        }
        buttons = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
        hat = buf[2];
        if (hat > PAD_HAT_NEUTRAL) {
            hat = PAD_HAT_NEUTRAL;
        }
        store_report(buttons & PAD_BTN_MASK, hat);
        mark_linked();
        return 0;

    default:
        return 0;
    }
}

/* [note_attempt] Marca o instante desta tentativa de conexão. */
static void note_attempt(void)
{
    s_attempt_us = esp_timer_get_time();
}

/* [start_scan] Procura o service do Pad Emu. Anúncios repetidos passam. Se o rádio recusar, [watch_cb] tenta de novo. */
static void start_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params params = {0};
    int rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        set_link(PAD_LINK_DOWN);
        s_phase = PHASE_IDLE;
        return;
    }
    params.filter_duplicates = 0;
    params.passive = 0;
    s_phase = PHASE_SCAN;
    set_link(PAD_LINK_SCANNING);
    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc == BLE_HS_EALREADY) {
        return;
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "scan failed: %d", rc);
        s_phase = PHASE_IDLE;
        set_link(PAD_LINK_DOWN);
    }
}

/* [watch_cb] Religa um scan morto e larga um connect ou discover parado há mais de 8 s. */
static void watch_cb(struct ble_npl_event *ev)
{
    int64_t age = esp_timer_get_time() - s_attempt_us;

    (void)ev;
    if (s_phase == PHASE_IDLE || (s_phase == PHASE_SCAN && !ble_gap_disc_active())) {
        start_scan();
    } else if (s_phase == PHASE_CONNECTING && age > 8000000) {
        ESP_LOGW(TAG, "connect timed out");
        ble_gap_conn_cancel();
        start_scan();
    } else if (s_phase == PHASE_DISCOVER && age > 8000000) {
        ESP_LOGW(TAG, "discover timed out");
        if (!s_have_conn || ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM) != 0) {
            s_have_conn = 0;
            clear_buttons();
            start_scan();
        }
    }
    ble_npl_callout_reset(&s_watch, ble_npl_time_ms_to_ticks32(2000));
}

/* [on_reset] Marca o host como caído. */
static void on_reset(int reason)
{
    ESP_LOGE(TAG, "host reset: %d", reason);
    s_have_conn = 0;
    s_phase = PHASE_IDLE;
    set_link(PAD_LINK_DOWN);
}

/* [on_sync] Sobe o endereço e chama [start_scan]. */
static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "no address: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "ble controller up");
    start_scan();
}

/* [host_task] Roda o laço do NimBLE. */
static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* [pad_link_start] Sobe o NimBLE central e procura o Pad Emu. O NVS já está de pé por [nvs_start]. */
void pad_link_start(void)
{
    ESP_ERROR_CHECK(nimble_port_init());

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;
    ble_store_config_init();
    ble_npl_callout_init(&s_watch, nimble_port_get_dflt_eventq(), watch_cb, NULL);
    nimble_port_freertos_init(host_task);
    ble_npl_callout_reset(&s_watch, ble_npl_time_ms_to_ticks32(2000));
}
