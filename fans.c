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

#define MAX_TEMP 110.0
#define MIN_TEMP 85.0

int main(int argc, char **argv) {
	pid_t pid, sid;

	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}

	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	umask(0);
	FILE *log_f = fopen("/var/log/fans.log", "w");
	fprintf(log_f, "fansd - launched\n");

	sid = setsid();
	if (sid < 0) {
		fprintf(log_f, "bad sid - exiting\n");
		fclose(log_f);
		exit(EXIT_FAILURE);
	}

	if (chdir("/") < 0) {
		fprintf(log_f, "error changing to / - exiting\n");
		fclose(log_f);
		exit(EXIT_FAILURE);
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	FILE *ctl_f, *speed_f[3], *min_f, *max_f;
	size_t ctl_len = strlen("/sys/devices/platform/applesmc.768/fanX_manual");
	size_t speed_len = strlen("/sys/devices/platform/applesmc.768/fanX_output");
	size_t min_len = strlen("/sys/devices/platform/applesmc.768/fanX_min");
	int min[3], max[3];

	for (int i = 0; i < 3; i++) {
		char *ctl_fname = malloc(ctl_len+1);
		sprintf(ctl_fname, "/sys/devices/platform/applesmc.768/fan%d_manual", i+1);
		ctl_f = fopen(ctl_fname, "w");
		fputc('1', ctl_f);
		free(ctl_fname);
		fclose(ctl_f);

		char *speed_fname = malloc(speed_len+1);
		sprintf(speed_fname, "/sys/devices/platform/applesmc.768/fan%d_output", i+1);
		speed_f[i] = fopen(speed_fname, "a");
		free(speed_fname);


		char *min_fname = malloc(min_len+1);
		sprintf(min_fname, "/sys/devices/platform/applesmc.768/fan%d_min", i+1);
		min_f = fopen(min_fname, "r");
		free(min_fname);
		char min_s[20];
		fgets(min_s, 19, min_f);
		min[i] = atoi(min_s);
		fclose(min_f);


		char *max_fname = malloc(min_len+1);
		sprintf(max_fname, "/sys/devices/platform/applesmc.768/fan%d_max", i+1);
		max_f = fopen(max_fname, "r");
		free(max_fname);
		char max_s[20];
		fgets(max_s, 19, max_f);
		max[i] = atoi(max_s);
		fclose(max_f);

		fprintf(log_f, "Fan %d configuration\n============\nMin RPM: %d Max RPM: %d\n\n", i+1, min[i], max[i]);
	}
	
	char temp_s[20];
	double temp;
	while (1) {
		FILE *temp_f = fopen("/sys/devices/platform/applesmc.768/temp7_input", "r");
		fgets(temp_s, 19, temp_f);
		temp = atoi(temp_s)*(9.0/5.0)/1000.0 + 32.0;
		double pct = ((double)temp-MIN_TEMP)/(MAX_TEMP-MIN_TEMP);
		if (pct < 0.0) {
			pct = 0.0;
		} else if (pct > 1.0) {
			pct = 1.0;
		}
		fprintf(log_f, "TEMP: %.2f ", temp);
		for (int i = 0; i < 3; i++) {
			int speed = min[i] + (max[i] - min[i])*pct;
			fseek(speed_f[i], 0, SEEK_SET);
			fprintf(speed_f[i], "%d", speed);
			fprintf(log_f, "FAN %d: %d RPM ", i+1, speed);
		}
		fprintf(log_f, "\n");
		fclose(temp_f);
		fflush(log_f);
		sleep(5);
	}

	for (int i = 0; i < 3; i++) {
		fclose(speed_f[i]);
	}
	fclose(log_f);
	return EXIT_SUCCESS;
}
