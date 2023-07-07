#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <curl/curl.h>

#define DATA_FILE "/var/datamonitor"
#define DATA_WARN 1000000000000
#define WEBHOOK_URL "https://discord.com/api/webhooks/[your webhook]"

void send_message(const char *msg) {
	curl_global_init(CURL_GLOBAL_SSL);
	CURL *handle = curl_easy_init();
	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	curl_easy_setopt(handle, CURLOPT_POSTFIELDS, msg);
	curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(handle, CURLOPT_URL, WEBHOOK_URL);
	curl_easy_perform(handle);
	curl_slist_free_all(headers);
}

int main(int argc, const char **argv) {

	// Check for provided interface name
	if(argc != 2) {
		fprintf(stderr, "Usage: %s interface\n", argv[0]);
		return 1;
	}



	// Make sure interface name has no whitespace
	const char *interface = argv[1];

	regex_t whitespace;
	assert(regcomp(&whitespace, "[\r\n\t\f\v ]+", REG_EXTENDED | REG_NOSUB) == 0);

	if(!regexec(&whitespace, interface, 0, NULL, 0)) {
		fprintf(stderr, "Illegal interface name\n");
		return 1;
	}

	regfree(&whitespace);



	// Get interface stats from pfctl
	regex_t parse;
	assert(regcomp(&parse, "(In|Out)([46])\\/Pass.+Bytes: ([0-9]+)", REG_EXTENDED) == 0);
	regmatch_t matches[4];

	long long in4 = -1, in6 = -1, out4 = -1, out6 = -1;

	char buf[1024];
	snprintf(buf, 1024, "/sbin/pfctl -vvsI -i %s", interface);
	FILE *pfctl = popen(buf, "r");

	while(!feof(pfctl)) {
		fgets(buf, 1024, pfctl);
		if(!regexec(&parse, buf, 4, matches, 0)) {
			const char *inout = buf + matches[1].rm_so;
			const char *ipversion = buf + matches[2].rm_so;
			const char *bytes = buf + matches[3].rm_so;

			long long bytes_int = atoll(bytes);

			if(strstr(inout, "In") == inout) {
				if(*ipversion == '4') in4 = bytes_int;
				else if(*ipversion == '6') in6 = bytes_int;
			} else if(strstr(inout, "Out") == inout) {
				if(*ipversion == '4') out4 = bytes_int;
				else if(*ipversion == '6') out6 = bytes_int;
			}
		}
	}

	regfree(&parse);

	int ret = pclose(pfctl);
	if(ret) {
		fprintf(stderr, "pfctl returned nonzero exit code\n");
		return ret;
	}

	if(in4 < 0 || in6 < 0 || out4 < 0 || out6 < 0) {
		fprintf(stderr, "Failed to parse output of pfctl\n");
		return 1;
	}



	// Determine when data file was last updated
	time_t now = time(NULL);
	time_t mtime = now;

	struct stat file_info;
	if(!stat(DATA_FILE, &file_info))
		mtime = file_info.st_mtime;



	// Load content of data file
	FILE *datafile = fopen(DATA_FILE, "r");

	// Start of month values
	long long start_in4 = 0, start_in6 = 0, start_out4 = 0, start_out6 = 0;

	// Previous day values
	long long old_in4 = in4, old_in6 = in6, old_out4 = out4, old_out6 = out6;

	// Values from file to be loaded if the file isn't corrupt
	long long new_vals[8];

	int ind;
	for(ind = 0; ind < 8 && datafile && !feof(datafile); ind++) {
		fgets(buf, 1024, datafile);
		new_vals[ind] = atoll(buf);
	}

	if(ind == 8) {
		start_in4 = new_vals[0];
		start_in6 = new_vals[1];
		start_out4 = new_vals[2];
		start_out6 = new_vals[3];
		old_in4 = new_vals[4];
		old_in6 = new_vals[5];
		old_out4 = new_vals[6];
		old_out6 = new_vals[7];
	}

	if(datafile) fclose(datafile);
	datafile = fopen(DATA_FILE, "w");
	if(!datafile) {
		perror("Failed to open " DATA_FILE " for writing");
		return 1;
	}



	// Check if now is a different month from the last saved month

	struct tm *date_info = localtime(&mtime);
	int saved_month = date_info->tm_mon;

	date_info = localtime(&now);
	int this_month = date_info->tm_mon;

	// Compute traffic volume recorded in file
	long long total_in4 = old_in4 - start_in4,
		  total_in6 = old_in6 - start_in6,
		  total_out4 = old_out4 - start_out4,
		  total_out6 = old_out6 - start_out6;

	if(this_month != saved_month) {
		// Report this month's traffic volume
		long long total_total = total_in4 + total_in6 + total_out4 + total_out6;
		snprintf(buf, 1024, "{\"content\": \"**Past Month's Traffic**\\n\\tIn v4: %.02fGB\\n\\tIn v6: %.02fGB\\n\\tOut v4: %.02fGB\\n\\tOut v6: %.02f\\n\\t**Total: %.02fGB**\\n\"}", total_in4/1e9, total_in6/1e9, total_out4/1e9, total_out6/1e9, total_total/1e9);
		send_message(buf);

		// Update data file
		fprintf(datafile, "%lli\n%lli\n%lli\n%lli\n%lli\n%lli\n%lli\n%lli\n", in4, in6, out4, out6, in4, in6, out4, out6);
		fclose(datafile);

		return 0;
	}



	// Compute old total
	long long total_total_old = total_in4 + total_in6 + total_out4 + total_out6;

	// Check if pfctl's counters have reset
	if(in4 < old_in4 || in6 < old_in6 || out4 < old_out4 || out6 < old_out6) {
		start_in4 = -total_in4;
		start_in6 = -total_in6;
		start_out4 = -total_out4;
		start_out6 = -total_out6;

		// Add today's usage
		total_in4 += in4;
		total_in6 += in6;
		total_out4 += out4;
		total_out6 += out6;
	} else {
		// Add today's usage
		total_in4 += in4 - old_in4;
		total_in6 += in6 - old_in6;
		total_out4 += out4 - old_out4;
		total_out6 += out6 - old_out6;
	}

	// Compute total including today
	long long total_total = total_in4 + total_in6 + total_out4 + total_out6;

	// Send warning if we're exceeding the limit
	if(total_total >= DATA_WARN && total_total_old < DATA_WARN) {
		snprintf(buf, 1024, "{\"content\": \"**WARNING: Data usage exceeding limit!**\\n\\tIn v4: %.02fGB\\n\\tIn v6: %.02fGB\\n\\tOut v4: %.02fGB\\n\\tOut v6: %.02f\\n\\t**Total: %.02fGB**\\n\"}", total_in4/1e9, total_in6/1e9, total_out4/1e9, total_out6/1e9, total_total/1e9);
		send_message(buf);
	}

	// Update data file
	fprintf(datafile, "%lli\n%lli\n%lli\n%lli\n%lli\n%lli\n%lli\n%lli\n", start_in4, start_in6, start_out4, start_out6, in4, in6, out4, out6);
	fclose(datafile);

	return 0;
}
