#ifndef ESPECTER_BATTERY_H
#define ESPECTER_BATTERY_H

#ifdef __cplusplus
extern "C" {
#endif

/* [battery_start] Abre o ADC1 no GPIO1 e toma a primeira leitura. O pack é o ADC vezes 3. */
void battery_start(void);

/* [battery_percent] Devolve 0..100. Relê o ADC no máximo uma vez por segundo. Devolve -1 se [battery_start] não abriu o ADC. */
int battery_percent(void);

#ifdef __cplusplus
}
#endif

#endif
