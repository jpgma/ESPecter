#include "battery.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "especter";

enum {
    VBAT_CHAN = ADC_CHANNEL_0,
    VBAT_SCALE = 3,
    VBAT_READS = 8,
    VBAT_PERIOD_US = 1000000,
};

/* Curva de repouso de uma célula, em milivolts do pack. */
static const int s_curve_mv[] = {
    3270, 3610, 3690, 3710, 3730, 3750, 3770, 3790,
    3800, 3820, 3840, 3850, 3870, 3910, 3950, 3980,
    4020, 4080, 4110, 4150, 4200,
};

static const int s_curve_pct[] = {
    0, 5, 10, 15, 20, 25, 30, 35,
    40, 45, 50, 55, 60, 65, 70, 75,
    80, 85, 90, 95, 100,
};

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static int s_ready;
static int s_pct = -1;
static int64_t s_us;

/* [percent_from_mv] Mapeia milivolts do pack para 0..100 na curva de repouso. */
static int percent_from_mv(int mv)
{
    int i;
    const int n = (int)(sizeof s_curve_mv / sizeof s_curve_mv[0]);

    if (mv <= s_curve_mv[0]) {
        return 0;
    }
    if (mv >= s_curve_mv[n - 1]) {
        return 100;
    }
    for (i = 1; i < n; i++) {
        int span;

        if (mv > s_curve_mv[i]) {
            continue;
        }
        span = s_curve_mv[i] - s_curve_mv[i - 1];
        return s_curve_pct[i - 1]
            + (mv - s_curve_mv[i - 1]) * (s_curve_pct[i] - s_curve_pct[i - 1]) / span;
    }
    return 100;
}

/* [sample_pack_mv] Devolve a média de oito leituras calibradas vezes 3, ou -1 se o ADC falhar. */
static int sample_pack_mv(void)
{
    int i;
    int sum = 0;
    int n = 0;

    for (i = 0; i < VBAT_READS; i++) {
        int raw = 0;
        int mv = 0;

        if (adc_oneshot_read(s_adc, VBAT_CHAN, &raw) != ESP_OK) {
            continue;
        }
        if (s_cali == NULL || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
            continue;
        }
        sum += mv;
        n += 1;
    }
    if (n == 0) {
        return -1;
    }
    return (sum / n) * VBAT_SCALE;
}

/* [battery_refresh] Atualiza o percentual se passou um segundo desde a última leitura. */
static void battery_refresh(void)
{
    int64_t now;
    int pack;

    if (!s_ready) {
        return;
    }
    now = esp_timer_get_time();
    if (s_pct >= 0 && now - s_us < VBAT_PERIOD_US) {
        return;
    }
    pack = sample_pack_mv();
    s_us = now;
    if (pack < 0) {
        return;
    }
    s_pct = percent_from_mv(pack);
}

/* [battery_start] Abre o ADC1 no GPIO1 e toma a primeira leitura. O pack é o ADC vezes 3. */
void battery_start(void)
{
    adc_oneshot_unit_init_cfg_t unit = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    adc_oneshot_chan_cfg_t chan = {
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_curve_fitting_config_t cali = {
        .unit_id = ADC_UNIT_1,
        .chan = VBAT_CHAN,
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    int pack;

    if (adc_oneshot_new_unit(&unit, &s_adc) != ESP_OK) {
        ESP_LOGW(TAG, "vbat adc unit failed");
        return;
    }
    if (adc_oneshot_config_channel(s_adc, VBAT_CHAN, &chan) != ESP_OK) {
        ESP_LOGW(TAG, "vbat adc channel failed");
        return;
    }
    if (adc_cali_create_scheme_curve_fitting(&cali, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "vbat adc cali failed");
        return;
    }
    s_ready = 1;
    pack = sample_pack_mv();
    s_us = esp_timer_get_time();
    if (pack < 0) {
        ESP_LOGW(TAG, "vbat adc read failed");
        return;
    }
    s_pct = percent_from_mv(pack);
    ESP_LOGI(TAG, "battery %d%% %d mV", s_pct, pack);
}

/* [battery_percent] Devolve 0..100. Relê o ADC no máximo uma vez por segundo. Devolve -1 se [battery_start] não abriu o ADC. */
int battery_percent(void)
{
    battery_refresh();
    return s_pct;
}
