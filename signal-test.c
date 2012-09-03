#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <curses.h>
#include <math.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/version.h>




static __u32 ber;
static __u16 snr;
static __u16 signal_strength;
static int has_signal;
static int synced;
static int locked;


static unsigned long lnb_low_val;
static unsigned long lnb_high_val;
static unsigned long lnb_switch_val;


static int selected;
static int total_channels;



struct channel_info {
	char name[50];
	int freq;
	int sr;
	int sat_no;
	int pol;
};


struct diseqc_cmd {
	struct dvb_diseqc_master_cmd cmd;
	uint32_t wait;
};




void diseqc_send_msg(int fd, fe_sec_voltage_t v, struct diseqc_cmd *cmd,
		     fe_sec_tone_mode_t t, fe_sec_mini_cmd_t b)
{
	if (ioctl(fd, FE_SET_TONE, SEC_TONE_OFF) == -1)
		perror("FE_SET_TONE failed");
	if (ioctl(fd, FE_SET_VOLTAGE, v) == -1)
		perror("FE_SET_VOLTAGE failed");
		usleep(15 * 1000);
	if (ioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &cmd->cmd) == -1)
		perror("FE_DISEQC_SEND_MASTER_CMD failed");
		usleep(cmd->wait * 1000);
		usleep(15 * 1000);
	if (ioctl(fd, FE_DISEQC_SEND_BURST, b) == -1)
		perror("FE_DISEQC_SEND_BURST failed");
		usleep(15 * 1000);
	if (ioctl(fd, FE_SET_TONE, t) == -1)
		perror("FE_SET_TONE failed");
}


/* digital satellite equipment control,
 * specification is available from http://www.eutelsat.com/
 */
static int diseqc(int secfd, int sat_no, int pol_vert, int hi_band)
{
	struct diseqc_cmd cmd =
		{ {{0xe0, 0x10, 0x38, 0xf0, 0x00, 0x00}, 4}, 0 };

	/**
	 * param: high nibble: reset bits, low nibble set bits,
	 * bits are: option, position, polarizaion, band
	 */
	cmd.cmd.msg[3] =
		0xf0 | (((sat_no * 4) & 0x0f) | (hi_band ? 1 : 0) | (pol_vert ? 0 : 2));

	diseqc_send_msg(secfd, pol_vert ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18,
			&cmd, hi_band ? SEC_TONE_ON : SEC_TONE_OFF,
			(sat_no / 4) % 2 ? SEC_MINI_B : SEC_MINI_A);

	return 1;
}




// lifted from  szap.c  in the dvb-apps package
struct channel_info *read_channels(const char *filename)
{
	FILE *cfp;
	char buf[4096];
	char *tmp;
	char *field;
	
	static struct channel_info list[1000];
	
	
	
	if (!(cfp = fopen(filename, "r"))) {
		printf("error opening channel list '%s'\n", filename);
		return NULL;
	}
	
	
	char tmp_buf[4096];
	int count = 0;
	while (!feof(cfp))
	{
		fgets(tmp_buf, sizeof(tmp_buf), cfp);
		count++;
	}
	
	
	rewind(cfp);
	
	int line = 0;
	while (!feof(cfp)) {
		
		if (fgets(buf, sizeof(buf), cfp)) {
			
		
		line++;
		tmp = buf;
		
		
		if (!(field = strsep(&tmp, ":")))
			goto syntax_err;
		
		strncpy (list[line-1].name, field, 50);
		list[line-1].name[49] = '\0';
		
		
		
		if (!(field = strsep(&tmp, ":")))
			goto syntax_err;
		
		list[line-1].freq = strtoul(field, NULL, 0) * 1000;

		
		if (!(field = strsep(&tmp, ":")))
			goto syntax_err;
		
		list[line-1].pol = (field[0] == 'h' ? 0 : 1);
		
		
		if (!(field = strsep(&tmp, ":")))
			goto syntax_err;
		
		list[line-1].sat_no = strtoul(field, NULL, 0);
		
		
		if (!(field = strsep(&tmp, ":")))
			goto syntax_err;
		
		list[line-1].sr = strtoul(field, NULL, 0) * 1000;

		
		continue;

syntax_err:
		printf("syntax error at line: %d\n", line);
	} else if (ferror(cfp)) {
		printf("error reading channel list '%s'\n",
		filename);
		fclose(cfp);
		return NULL;
	} else
		break;
	}

	fclose(cfp);
	
	total_channels = line;
	return list;
}






void init_screen()
{
	initscr();
	cbreak();
	noecho();
	nodelay(stdscr, TRUE);
	keypad(stdscr, TRUE);
	curs_set(0);
}



void quit(char *error)
{
	endwin();
	printf("%s\n", error);
}



int open_frontend()
{
	static int frontend;
	if ((frontend = open("/dev/dvb/adapter0/frontend0", O_RDWR | O_NONBLOCK)) < 0) {
			printf("opening frontend failed\n");
			return 0;
	}
	
	return frontend;
}



unsigned int tune(int frontend, struct channel_info info)
{
	struct dvbfe_params params;
	struct dvbfe_info fe_info;
	
	uint32_t ifreq;
	int hiband;
	int freq = info.freq;
	
	
	//lnb setup
	hiband = 0;
	if (freq >= lnb_switch_val)
		hiband = 1;

	if (hiband)
		ifreq = freq - lnb_high_val;
	else {
		if (freq < lnb_low_val)
			ifreq = lnb_low_val - freq;
	else
		ifreq = freq - lnb_low_val;
	}
	
	
	if (diseqc(frontend, info.sat_no, info.pol, hiband) < 0)
	{
		quit("cannot set the lnb");
		return 0;
	}
	
	
	
	
	//data to tune to
	params.frequency = ifreq;
	params.delsys.dvbs.symbol_rate = info.sr;
	params.delsys.dvbs.modulation = DVBFE_MOD_AUTO;
	params.delsys.dvbs.fec = DVBFE_FEC_AUTO;
	params.inversion = DVBFE_INVERSION_AUTO;
	
	
	// set the tuning parameters
	if (ioctl(frontend, DVBFE_SET_PARAMS, &params) < 0) {
		quit("cannot set parameters");
		return 0;
	}
	
	
	return ifreq;
}


int get_signal_details(int frontend)
{
	
	static fe_status_t status;
	
	
	if (ioctl(frontend, FE_READ_STATUS, &status) < 0) {
		quit("no response from the card");
		return 0;
	}
	
	if (ioctl(frontend, FE_READ_BER, &ber) < 0) {
		quit("cannot get the ber");
		return 0;
	}
	
	if (ioctl(frontend, FE_READ_SNR, &snr) < 0) {
		quit("cannot get the snr");
		return 0;
	}
	
	if (ioctl(frontend, FE_READ_SIGNAL_STRENGTH, &signal_strength) < 0) {
		quit("cannot get the signal");
		return 0;
	}
	
	
	
	switch (status)
	{
		case FE_HAS_SIGNAL:
			has_signal = 1;
		case FE_HAS_SYNC:
			synced = 1;
		case FE_HAS_LOCK:
			locked = 1;
	}
	
	return 1;
}


void start_interface (struct channel_info *list)
{
	
	int frontend = open_frontend();
	if (frontend == 0)
	{
		printf("cannot get frontend info");
		return;
	}
	
	
	struct dvbfe_info fe_info;
	enum dvbfe_delsys delivery = DVBFE_DELSYS_DVBS;
	uint32_t ifreq = 0;
	
	
	//must first set the delivery system
	if (ioctl(frontend, DVBFE_SET_DELSYS, &delivery) < 0) {
		printf("cannot set delivery system");
		close(frontend);
		return;
	}
	
	//required to actually set the delsys
	if (ioctl(frontend, DVBFE_GET_INFO, &fe_info) < 0) {
		printf("cannot get frontend info");
		close(frontend);
		return;
	}
	
	
	lnb_low_val = 9750000;
	lnb_high_val = 10600000;
	lnb_switch_val = 11700000;
	
	
	WINDOW *channels;
	WINDOW *info;
	selected = 0;
	int tuned = -1;

	
	init_screen();
	
	channels = newwin(20, 40, 1, 1);
	info = newwin(20, 40, 1, 50);
	
	refresh();
	
	
	int start = 0;
	for (;;) {
		
		// channel list
		
		box(channels, ACS_VLINE, ACS_HLINE);
		mvwprintw(channels, 0, 15, "Channels");
		
		int i;
		
		if (selected > 17 && selected > start + 17)
			start = selected - 17;
		else if (selected < start)
			start = selected;
		
		
		int end = start + 18;
		int pos = 1;
		for (i=start; i<end; i++)
		{
			
			if (i == selected)
				mvwprintw(channels, pos, 1, "--> %s", list[i].name);
			else
				mvwprintw(channels, pos, 1, "    %s", list[i].name);
			
			pos++;
		}
		
		
		// signal details
		if (tuned>0 && get_signal_details (frontend) == 0)
		{
			quit("cannot get the signal details");
			close(frontend);
			return;
		}
		
		werase (info);
		
		
		box(info, ACS_VLINE, ACS_HLINE);
		
		mvwprintw(info, 0, 12, "Signal Details");
		mvwprintw(info, 2, 4, "DVB API v%d.%d", DVB_API_VERSION, DVB_API_VERSION_MINOR);

		mvwprintw(info, 4, 4, "Channel:         %s", tuned>0 ? list[tuned].name : "");
		mvwprintw(info, 5, 4, "Frequency:       %d", ifreq);
		mvwprintw(info, 6, 4, "Polarity:        %s", tuned>0 ? (list[tuned].pol == 0 ? "Horizontal" : "Vertical") : "");
		mvwprintw(info, 7, 4, "Symbol Rate:     %d", tuned>0 ? list[tuned].sr : 0);
		mvwprintw(info, 8, 4, "Delivery System: DVB-S");
		
		
		mvwprintw(info, 11, 4, "Signal:   %s", has_signal ? "Yes" : "No");
		mvwprintw(info, 12, 4, "Synced:   %s", synced ? "Yes" : "No");
		mvwprintw(info, 13, 4, "Locked:   %s", locked ? "Yes" : "No");
		
		
		double snr_human = log10(snr);
		int signal_strength_human = signal_strength*100;


		mvwprintw(info, 15, 4, "Signal Noise Ratio:   %d dB", snr_human);
		mvwprintw(info, 16, 4, "Signal Strength:      %u%%", signal_strength_human);
		mvwprintw(info, 17, 4, "Bit Error Rate:       %u", ber);
		
		wrefresh(channels);
		wrefresh(info);
		refresh();
		
		
		
		
		int break_loop = 0;
		
		int key = getch();
		switch (key)
		{
		
		case 113:   // the letter 'q'
			break_loop = 1;
			break;
		
		case KEY_UP:
			if (selected > 0)
			{
				selected--;
				werase (channels);
			}
			break;
			
		case KEY_DOWN:
			if (selected < total_channels-1)
			{
				selected++;
				werase (channels);
			}
			break;
		
		case 10:
			ifreq = tune (frontend, list[selected]);
			if (ifreq == 0)
			{
				quit("cannot tune to the specified frequency");
				close(frontend);
				return;
			}
			tuned = selected;
			break;
			
		}
		
		if (break_loop)
			break;
	}
	

	quit("");
	close(frontend);
}





int main(int argc, char *argv[])
{
	printf("Reading file '%s'\n", argv[1]);
	
	
	struct channel_info *list = read_channels(argv[1]);
	
	if (list == NULL)
		return -1;
	
	start_interface (list);
	
}

