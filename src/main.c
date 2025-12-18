#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdlib.h>

#ifdef CONFIG_MYFUNCTION
#include "myfunction.h"
#endif

#define GPS_UART_NODE DT_NODELABEL(uart1)
#define GPS_RX_BUF_SIZE 256

static const struct device *gps_uart = DEVICE_DT_GET(GPS_UART_NODE);
static char gps_rx_buf[GPS_RX_BUF_SIZE];
static int gps_rx_buf_pos = 0;

// Структура для хранения GPS данных
struct gps_data {
	char time[16];
	char latitude[16];
	char lat_dir;
	char longitude[16];
	char lon_dir;
	int fix_quality;
	int satellites;
	float hdop;
	float altitude;
	float speed_knots;
	float course;
	bool valid;
} current_gps;

// Конвертация NMEA координат в десятичные градусы
static float nmea_to_degrees(const char *nmea_coord, bool is_longitude)
{
	if (strlen(nmea_coord) < 4) {
		return 0.0f;
	}
	
	float coord = atof(nmea_coord);
	int degrees;
	float minutes;
	
	if (is_longitude) {
		// Долгота: DDDMM.MMMMM
		degrees = (int)(coord / 100.0f);
		minutes = coord - (degrees * 100.0f);
	} else {
		// Широта: DDMM.MMMMM
		degrees = (int)(coord / 100.0f);
		minutes = coord - (degrees * 100.0f);
	}
	
	return degrees + (minutes / 60.0f);
}

// Парсинг NMEA строки
static void parse_nmea(char *sentence)
{
	char *token;
	char *saveptr;
	int field = 0;
	
	// Копируем строку для безопасного парсинга
	char buf[GPS_RX_BUF_SIZE];
	strncpy(buf, sentence, GPS_RX_BUF_SIZE - 1);
	buf[GPS_RX_BUF_SIZE - 1] = '\0';
	
	// Определяем тип сообщения
	if (strstr(buf, "$GNGGA") == buf || strstr(buf, "$GPGGA") == buf) {
		// GGA - координаты и качество фикса
		token = strtok_r(buf, ",", &saveptr);
		while (token != NULL && field < 15) {
			switch (field) {
				case 1: // Время UTC
					if (strlen(token) >= 6) {
						snprintf(current_gps.time, sizeof(current_gps.time), 
						        "%c%c:%c%c:%c%c", 
						        token[0], token[1], token[2], token[3], token[4], token[5]);
					}
					break;
				case 2: // Широта
					strncpy(current_gps.latitude, token, sizeof(current_gps.latitude) - 1);
					break;
				case 3: // N/S
					current_gps.lat_dir = token[0];
					break;
				case 4: // Долгота
					strncpy(current_gps.longitude, token, sizeof(current_gps.longitude) - 1);
					break;
				case 5: // E/W
					current_gps.lon_dir = token[0];
					break;
				case 6: // Качество фикса
					current_gps.fix_quality = atoi(token);
					break;
				case 7: // Количество спутников
					current_gps.satellites = atoi(token);
					break;
				case 8: // HDOP
					current_gps.hdop = atof(token);
					break;
				case 9: // Высота
					current_gps.altitude = atof(token);
					break;
			}
			token = strtok_r(NULL, ",", &saveptr);
			field++;
		}
	} else if (strstr(buf, "$GNRMC") == buf || strstr(buf, "$GPRMC") == buf) {
		// RMC - скорость и курс
		token = strtok_r(buf, ",", &saveptr);
		while (token != NULL && field < 10) {
			switch (field) {
				case 2: // Статус A=valid, V=invalid
					current_gps.valid = (token[0] == 'A');
					break;
				case 7: // Скорость в узлах
					current_gps.speed_knots = atof(token);
					break;
				case 8: // Курс
					current_gps.course = atof(token);
					break;
			}
			token = strtok_r(NULL, ",", &saveptr);
			field++;
		}
	}
}

// Вывод читабельной информации о GPS
static void print_gps_info(void)
{
	printk("╔══════════════════════════════════════════════════════════════════╗\n");
	printk("║               ---СТАТУС GPS ---                                  ║\n");
	printk("╠══════════════════════════════════════════════════════════════════╣\n");
	
	// Статус
	printk("║ Статус:                                                             ");
	if (current_gps.valid && current_gps.fix_quality > 0) {
		printk("✓ ФИКС ПОЛУЧЕН                                    ║\n");
	} else {
		printk("✗ НЕТ ФИКСА (поиск спутников...)                 ║\n");
	}
	
	// Время
	if (strlen(current_gps.time) > 0) {
		printk("║ Время (UTC):   %s                                            ║\n", 
		       current_gps.time);
	}
	
	// Спутники
	printk("║ Спутников:     %d отслеживается                                  ║\n", 
	       current_gps.satellites);
	
	// Точность
	if (current_gps.hdop > 0 && current_gps.hdop < 50) {
		printk("║ Точность:      HDOP = %.2f ", current_gps.hdop);
		if (current_gps.hdop < 2.0) {
			printk("(Отлично)                       ║\n");
		} else if (current_gps.hdop < 5.0) {
			printk("(Хорошо)                        ║\n");
		} else if (current_gps.hdop < 10.0) {
			printk("(Средне)                        ║\n");
		} else {
			printk("(Плохо)                         ║\n");
		}
	}
	
	// Координаты
	if (current_gps.valid && strlen(current_gps.latitude) > 0) {
		float lat_deg = nmea_to_degrees(current_gps.latitude, false);
		float lon_deg = nmea_to_degrees(current_gps.longitude, true);
		
		// Применяем направление (S и W = отрицательные)
		if (current_gps.lat_dir == 'S') lat_deg = -lat_deg;
		if (current_gps.lon_dir == 'W') lon_deg = -lon_deg;
		
		printk("║ Широта:        %s %c                                   ║\n", 
		       current_gps.latitude, current_gps.lat_dir);
		printk("║                = %.6f°                                       ║\n", lat_deg);
		printk("║ Долгота:       %s %c                                  ║\n", 
		       current_gps.longitude, current_gps.lon_dir);
		printk("║                = %.6f°                                      ║\n", lon_deg);
		printk("║ Высота:        %.1f м                                        ║\n", 
		       current_gps.altitude);
		printk("║ Скорость:      %.2f узлов (%.2f км/ч)                        ║\n", 
		       current_gps.speed_knots, current_gps.speed_knots * 1.852);
		printk("║ Курс:          %.1f°                                         ║\n", 
		       current_gps.course);
	} else {
		printk("║ Позиция:       Ожидание фикса спутников...                      ║\n");
		printk("║                Переместите GPS модуль под открытое небо         ║\n");
	}
	
	printk("╚══════════════════════════════════════════════════════════════════╝\n\n");
}

// Callback для приема данных от GPS
static void gps_uart_cb(const struct device *dev, void *user_data)
{
	uint8_t c;
	static uint32_t sentence_count = 0;

	if (!uart_irq_update(gps_uart)) {
		return;
	}

	if (!uart_irq_rx_ready(gps_uart)) {
		return;
	}

	while (uart_fifo_read(gps_uart, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && gps_rx_buf_pos > 0) {
			// Конец строки NMEA
			gps_rx_buf[gps_rx_buf_pos] = '\0';
			
			// Парсим только важные сообщения
			if (strstr(gps_rx_buf, "$GNGGA") || strstr(gps_rx_buf, "$GPGGA") ||
			    strstr(gps_rx_buf, "$GNRMC") || strstr(gps_rx_buf, "$GPRMC")) {
				parse_nmea(gps_rx_buf);
				
				// Выводим сводку каждые 10 сообщений (~10 секунд)
				sentence_count++;
				if (sentence_count >= 10) {
					print_gps_info();
					sentence_count = 0;
				}
			}
			
			gps_rx_buf_pos = 0;
		} else if (c != '\r' && c != '\n') {
			// Добавляем символ в буфер
			if (gps_rx_buf_pos < GPS_RX_BUF_SIZE - 1) {
				gps_rx_buf[gps_rx_buf_pos++] = c;
			} else {
				// Переполнение буфера
				gps_rx_buf_pos = 0;
			}
		}
	}
}

int main(void)
{
	int a=3, b=4;

	// Инициализация структуры GPS
	memset(&current_gps, 0, sizeof(current_gps));

	// Инициализация GPS UART
	if (!device_is_ready(gps_uart)) {
		printk("GPS UART device not ready\n");
		return -1;
	}

	// Настройка прерываний UART
	uart_irq_callback_user_data_set(gps_uart, gps_uart_cb, NULL);
	uart_irq_rx_enable(gps_uart);

	printk("\n╔══════════════════════════════════════════════════════════════════╗\n");
	printk("║              GPS МОДУЛЬ ИНИЦИАЛИЗИРОВАН                          ║\n");
	printk("╠══════════════════════════════════════════════════════════════════╣\n");
	printk("║ UART:        P0.06 (TX), P0.08 (RX)                              ║\n");
	printk("║ Скорость:    9600, 8N1                                           ║\n");
	printk("║ Протокол:    NMEA                                                ║\n");
	printk("╚══════════════════════════════════════════════════════════════════╝\n\n");
	printk("Ожидание данных от GPS...\n\n");

	while(1){
	#ifdef CONFIG_MYFUNCTION
		printk(">>> Сумма %d и %d равна %d\n\n", a, b, sum(a,b));
	#else
		printk("Функция myfunc не включена\n");
	#endif
		k_msleep(20000);  // Увеличен интервал до 20 секунд для лучшей читаемости
		
		
	}
	
	
	
	
	
}