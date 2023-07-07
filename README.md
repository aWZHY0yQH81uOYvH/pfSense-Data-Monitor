# Simple data volume monitor for pfSense

This is a very simple program that keeps track of roughly how much data your router passes through its WAN port each month, since some ISPs charge exorbitant amounts if you exceed their limit. Warnings are sent to a webhook URL (intended for Discord) when the limit is exceeded. Totals for the previous month are also sent to the webhook the first time the program is run each month.

## To use
 * Change the values of the three `#define`s at the top of the .c file. They set the webhook URL, the file which is used to keep track of the amount of data accumulated, and the warning limit in bytes.
 * Compile with `gcc` on FreeBSD using the flag `-lcurl` to link with the curl library (which is installed by default in pfSense). Put the compiled executable in `/usr/local/bin`.
 * Install the `cron` package in pfSense and add a job using the web UI that runs `/usr/local/bin/datamonitor [interface]` daily, where `[interface]` is the device name, not the human-readable name you give it in the pfSense web UI. This can be found by selecting the interface in the web UI and looking at the name in parentheses at the top. (e.g. "Interfaces / WAN (vtnet1)")

## How it works
The program is run daily by the periodic/cron utility in FreeBSD. The program calls `pfctl -vvsI -i [interface]` to get the number of bytes the firewall has passed through. This is the exact same command the pfSense web interface uses to populate the interface statistics widget. It keeps track of the amount of data reported every day and the amount of data at the start of the month in `/var/datamonitor`. If the amount of data reported is lower than it was the previous day, it assumes the router has been restarted and handles it accordingly. Since it is not continuously saving the amount of data passed, some data can be missed this way. Just don't download 100GB and then immediately restart your router.
