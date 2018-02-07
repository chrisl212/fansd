#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <stdarg.h>

#define MAX_TEMP 110.0
#define MIN_TEMP 85.0
#define VMAJ 0
#define VMIN 1

typedef struct _Fan {
	int rpm, num, min, max;
	FILE *speed_f;
} Fan;

void flog(char *fmt, ...) {
	time_t timer;
	char buf[26];
	struct tm *tm_info;
	va_list lst;

	time(&timer);
	tm_info = localtime(&timer);
	strftime(buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

	fprintf(stderr, "[%s] - ", buf);

	va_start(lst, fmt);
	vfprintf(stderr, fmt, lst);
	va_end(lst);
	fflush(stderr);
}

double proc_temp() {
	char temp_s[10];
	FILE *temp_f = fopen("/sys/devices/platform/applesmc.768/temp7_input", "r");
	if (!temp_f) {
		return -1.0;
	}
	fgets(temp_s, 10, temp_f);
	fclose(temp_f);

	return atoi(temp_s)*(9.0/5.0)/1000.0 + 32.0;
}

void fan_conf(double *min, double *max) {
	FILE *conf_f = fopen("/etc/fans/fans.conf", "r");
	if (!conf_f) {
		return;
	}
	char buf[5];
	fgets(buf, 5, conf_f);
	*max = (double)atoi(buf);
	fgets(buf, 5, conf_f);
	*min = (double)atoi(buf);

	fclose(conf_f);
}

int fan_manual(int fan, int opt) {
	size_t len = strlen("/sys/devices/platform/applesmc.768/fanX_manual");
	char *fname = malloc(len+1);

	sprintf(fname, "/sys/devices/platform/applesmc.768/fan%d_manual", fan);
	FILE *ctl_f = fopen(fname, "w");
	if (!ctl_f) {
		return EXIT_FAILURE;
	}
	if (fwrite(&opt, sizeof(opt), 1, ctl_f) != 1) {
		fclose(ctl_f);
		free(fname);
		return EXIT_FAILURE;	
	}

	fclose(ctl_f);
	free(fname);
	return EXIT_SUCCESS;
}

int fan_minmax(int fan, int *min, int *max) {
	if (!max || !min) {
		return EXIT_FAILURE;
	}

	size_t len = strlen("/sys/devices/platform/applesmc.768/fanX_mXX");
	char *fname = malloc(len+1);
	char buf[6];

	sprintf(fname, "/sys/devices/platform/applesmc.768/fan%d_min", fan);
	FILE *min_f = fopen(fname, "r");
	if (!min_f) {
		free(fname);
		return EXIT_FAILURE;
	}
	fgets(buf, 6, min_f);
	fclose(min_f);
	*min = atoi(buf);

	sprintf(fname, "/sys/devices/platform/applesmc.768/fan%d_max", fan);
	FILE *max_f = fopen(fname, "r");
	if (!max_f) {
		free(fname);
		return EXIT_FAILURE;
	}
	fgets(buf, 6, max_f);
	fclose(max_f);
	*max = atoi(buf);

	free(fname);
	return EXIT_SUCCESS;
}

Fan *fan_init(int fans) {
	Fan *fan_a = malloc(sizeof(*fan_a)*fans);
	int len = strlen("/sys/devices/platform/applesmc.768/fanX_output");
	for (int i = 0; i < fans; i++) {
		fan_a[i].num = i+1;
		if (fan_manual(i+1, 1)) {
			flog("ERROR: could not set manual fan control for fan %d\n", i+1);
			free(fan_a);
			return NULL;
		}

		char *speed_fname = malloc(len+1);
		sprintf(speed_fname, "/sys/devices/platform/applesmc.768/fan%d_output", i+1);
		fan_a[i].speed_f = fopen(speed_fname, "a");
		if (!fan_a[i].speed_f) {
			flog("ERROR: could not open output for fan %d\n", i+1);
			free(fan_a);
			return NULL;
		}
		free(speed_fname);

		if (fan_minmax(i+1, &(fan_a[i].min), &(fan_a[i].max))) { 
			flog("ERROR: error fetching min and max RPM for fan %d\n", i+1);
			free(fan_a);
			return NULL;
		}

		flog("Fan %d configuration\n\t\t\t============\n\t\t\tMin RPM: %d Max RPM: %d\n", i+1, fan_a[i].min, fan_a[i].max);
	}
	return fan_a;
}

void fan_close(Fan *fans, int cnt) {
	for (int i = 0; i < cnt; i++) {
		fan_manual(i+1, 0);
		fclose(fans[i].speed_f);
	}
	free(fans);
}

void fan_adjust(Fan *fans, int cnt, double temp, double temp_min, double temp_max) {
	double pct = ((double)temp-temp_min)/(temp_max-temp_min);
	if (pct < 0.0) {
		pct = 0.0;
	} else if (pct > 1.0) {
		pct = 1.0;
	}
	for (int i = 0; i < cnt; i++) {
		int speed = fans[i].min + (fans[i].max - fans[i].min)*pct;
		fseek(fans[i].speed_f, 0, SEEK_SET);
		fprintf(fans[i].speed_f, "%d", speed);
		fprintf(stderr, "FAN %d: %d RPM ", i+1, speed);
	}
}

int count_fans() {
	DIR *dir = opendir("/sys/devices/platform/applesmc.768/");
	if (!dir) {
		return -1;
	}
	struct dirent *entry = NULL;
	int fans = 0;

	while ((entry = readdir(dir))) {
		if (entry->d_name[0] == 'f') {
			fans++;
		}
	}

	closedir(dir);
	return fans/7;
}

int main(int argc, char **argv) {
	pid_t pid, sid;

	pid = fork();
	if (pid < 0) {
		flog("Error getting pid\n");
		exit(EXIT_FAILURE);
	}

	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	umask(0);

	sid = setsid();
	if (sid < 0) {
		flog("Error getting sid\n");
		exit(EXIT_FAILURE);
	}

	if (chdir("/") < 0) {
		flog("Error changing to root dir\n");
		exit(EXIT_FAILURE);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);

	freopen("/var/log/fans.log", "w", stderr);
	flog("fansd v%d.%d\n", VMAJ, VMIN);

	double temp_max = MAX_TEMP;
	double temp_min = MIN_TEMP;
	fan_conf(&temp_min, &temp_max);
	flog("Using max temp of %.1f and a min of %.1f\n", temp_max, temp_min);

	int fan_cnt = count_fans();
	if (fan_cnt == -1) {
		flog("ERROR: could not count fans\n");
		exit(EXIT_FAILURE);
	}
	Fan *fans = fan_init(fan_cnt);
	if (!fans) {
		flog("ERROR: could not initialize fans\n");
		exit(EXIT_FAILURE);
	}

	while (1) {
		double temp = proc_temp();
		if (temp < 0.0) {
			flog("ERROR: could not read temperature\n");
			exit(EXIT_FAILURE);
		}
		fprintf(stderr, "\r");
		flog("TEMP: %.2f ", temp);
		fan_adjust(fans, fan_cnt, temp, temp_min, temp_max);
		sleep(5);
	}

	fclose(stderr);
	fan_close(fans, fan_cnt);
	exit(EXIT_SUCCESS);
}
