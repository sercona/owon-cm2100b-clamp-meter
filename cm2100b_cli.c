/*
 * cm2100b_cli.c
 *
 * (c) 2023 linux-works
 *
 *  2023-nov-05: modified for the CM2100B clamp-meter from another owon-b35 example
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)			\
  ((byte) & 0x80 ? '1' : '0'),			\
    ((byte) & 0x40 ? '1' : '0'),		\
    ((byte) & 0x20 ? '1' : '0'),		\
    ((byte) & 0x10 ? '1' : '0'),		\
    ((byte) & 0x08 ? '1' : '0'),		\
    ((byte) & 0x04 ? '1' : '0'),		\
    ((byte) & 0x02 ? '1' : '0'),		\
    ((byte) & 0x01 ? '1' : '0') 


// tokens that mean non-numeric things
#define INF_OPEN   (-32767)  // numeric
#define INF_OPEN_S "-32767"  // string


// mode bit-masks
#define MODE_DC_VOLTS     0b00000000
#define MODE_AC_VOLTS     0b00000001
#define MODE_AC_AMPS      0b00000011
#define MODE_DC_AMPS      0b00000010
#define MODE_NCV          0b00001101
#define MODE_OHMS         0b00000100
#define MODE_CAPACITANCE  0b00000101
#define MODE_DIODE        0b00001010
#define MODE_CONTINUITY   0b00001011
#define MODE_FREQUENCY    0b00000110
#define MODE_PERCENT      0b00000111


unsigned int function, scale, decimal;
int measurement;
uint16_t reading[3];  // 3 groups of 16bit words (6 bytes total)
char tmp_val_buf[16];
char fixed_val_buf[16];

char help[] = " -a <address> [-l <filename>] [-d] [-q]\n"\
  "\t-h: This help\n"\
  "\t-a <address>: Set the address of the cm2100b meter, eg, -a 98:84:E3:CD:C0:E5\n"\
  "\t-l <filename>: Set logging and the filename for the log\n"\
  "\t-d: debug enabled\n"\
  "\t-q: quiet output\n"\
  "\n\n\texample: cm2100b_cli -a 98:84:E3:CD:C0:E5 -l obsdata.txt\n"\
  "\n";

char default_output[] = "cm2100b.txt";
uint8_t sigint_pressed;

struct glb {
  uint8_t debug;
  uint8_t quiet;
  uint16_t flags;
  char *log_filename;
  char *output_filename;
  char *cm2100b_address;
};


char *timestamp (void)
{
  static char time_str[32];
  char fract_part_s[16];
  struct timeval tv;
 
  gettimeofday(&tv, NULL);

  // trim to just 2 places after decimal
  sprintf(fract_part_s, "%ld", (1000*tv.tv_usec));
  fract_part_s[2] = '\0';
 
  sprintf(time_str, "%ld.%s", tv.tv_sec, fract_part_s);
 
  return time_str;
}


int init (struct glb *g)
{
  g->debug = 0;
  g->quiet = 0;
  g->flags = 0;
  g->output_filename = default_output;
  g->cm2100b_address = NULL;
  g->log_filename = NULL;

  return 0;
}


int parse_parameters (struct glb *g, int argc, char **argv)
{
  int i;

  for (i = 0; i < argc; i++) {

    if (argv[i][0] == '-') {

      switch (argv[i][1]) {
      case 'h':
	fprintf(stdout, "Usage: %s %s", argv[0], help);
	exit(1);
	break;


      case 'l':
	// enable logging
	i++;
	if (i < argc) g->log_filename = argv[i];
	else {
	  fprintf(stderr, "-l <log_filename>\n");
	  exit(1);
	}
	break;

	
      case 'a':
	// address of CM2100B
	i++;
	if (i < argc) g->cm2100b_address = argv[i];
	else {
	  fprintf(stderr, "-a <address>\n");
	  exit(1);
	}
	break;
	

      case  'd':
	g->debug = 1;
	break;

      case 'q':
	g->quiet = 1;
	break;


      default:
	break;
      } // switch
    }
  }

  return 0;
}



void handle_sigint (int a)
{
  sigint_pressed = 1;
}


//
// function_code, values and flags to string
//

char *funct_to_string (char *fixed_val_buf, int function, int scale, int measurement)
{
  static char full_value_s[64];
  char funct_s[32];
  
  memset(full_value_s, 0, 64);
  memset(funct_s, 0, 32);

  
  if (function == MODE_DC_VOLTS) {
    if (scale == 3) {
      strcpy(funct_s, "DC mV");
	  
    } else if (scale == 4) {
      strcpy(funct_s, "DC V");
	  
    } else {
      strcpy(funct_s, "??DC V");
    }

  } else if (function == MODE_AC_VOLTS) {
    if (scale == 4) {
      strcpy(funct_s, "AC V");
    } else {
      strcpy(funct_s, "??AC V");
    }

  } else if (function == MODE_AC_AMPS) {
    strcpy(funct_s, "AC A");

  } else if (function ==MODE_DC_AMPS ) {
    strcpy(funct_s, "DC A");

  } else if (function == MODE_NCV) {
    strcpy(funct_s, "NCV");

  } else if (function == MODE_OHMS) {
    if (measurement == INF_OPEN) {
      strcpy(funct_s, "Ohms Open");
      
    } else {
      if (scale == 4) {
	strcpy(funct_s, "Ohms");
	  
      } else if (scale == 5) {
	strcpy(funct_s, "K Ohms");
	  
      } else if (scale == 6) {
	strcpy(funct_s, "M Ohms");
	  
      } else {
	strcpy(funct_s, "??Ohms");
      }
    }

  } else if (function == MODE_CAPACITANCE) {
    if (scale == 1) {
      strcpy(funct_s, "nF");
	
    } else if (scale == 2) {
      strcpy(funct_s, "uF");
	
    } else {
      strcpy(funct_s, "??Farads");
    }
      
  } else if (function == MODE_DIODE) {
    if (measurement == INF_OPEN) {
      strcpy(funct_s, "Diode Open");
      
    } else {
      strcpy(funct_s, "Diode");
    }
      
  } else if (function == MODE_CONTINUITY) {
    if (measurement == INF_OPEN) {
      strcpy(funct_s, "Continuity Open");
	
    } else {
      strcpy(funct_s, "Continuity Closed");
    }
      
  } else if (function == MODE_FREQUENCY) {
    strcpy(funct_s, "Hz"); // frequency mode

  } else if (function == MODE_PERCENT) {
    strcpy(funct_s, "%");  // percent mode

  } else {
    strcpy(funct_s, "???");
  }



  // form the full string
  strcpy(full_value_s, fixed_val_buf);
  strcat(full_value_s, " ");
  strcat(full_value_s, funct_s);
    
  return(full_value_s);
}



int main (int argc, char **argv)
{
  char cmd[1024];
  FILE *fp, *fl;
  struct glb g;


  fp = fl = NULL;

  sigint_pressed = 0;
  signal(SIGINT, handle_sigint); 

  init(&g);
  
  parse_parameters(&g, argc, argv);

  
  /*
   * Sanity check our parameters
   */
  
  if (g.cm2100b_address == NULL) {
    fprintf(stderr, "Cm2100b clamp meter bluetooth address required, try 'sudo hcitool lescan' to get address\n");
    exit(1);
  }

  
  /*
   * All good... let's get on with the job
   *
   * First step, try to open a pipe to gattool so we
   * can read the bluetooth output from the clamp meter via STDIN
   *
   */
  
  snprintf(cmd, sizeof(cmd), "gatttool -b %s --char-read --handle 0x1b --listen", g.cm2100b_address);
  
  fp = popen(cmd, "r");
  if (fp == NULL) {
    fprintf(stderr, "Error executing '%s'\n", cmd);
    exit(1);
  } else {
    if (!g.quiet) {
      //fprintf(stderr, "Success (%s)\n", cmd);
    }
  }

  
  /*
   * If required, open the log file, in append mode
   *
   */
  
  if (g.log_filename) {
    fl = fopen(g.log_filename, "a");
    if (fl == NULL) {
      fprintf(stderr, "Couldn't open '%s' file to write/append, NO LOGGING\n", g.log_filename);
    }
  }



  /*
   * Keep reading, interpreting and converting data until someone
   * presses ctrl-c or there's an error
   */
  
  while (fgets(cmd, sizeof(cmd), fp) != NULL) {
    char *p, *q;
    uint8_t d[14];
    uint8_t i = 0;


    if (sigint_pressed) {
      if (fp) {
	pclose(fp);
      }
      
      fprintf(stdout, "Exit requested\n");
      fflush(stdout);
      
      exit(1);
    }

    if (g.debug) {
      //fprintf(stdout,"%s", cmd);  // return string 'Notification handle = 0x001b value: 24 f0 04 00 c6 3a '
    }


    // this is ugly..  we search for a string in the gatttool return buffer, based on the handle
    p = strstr(cmd, "0x001b value: ");
    if (!p) {
      continue;
    }
    
    if (p) {
      p += sizeof("0x001b value:");
    }

    
    
    // changed this from 43 bytes to 19 (which is what this clamp-meter uses for gatttool payload string
    if (strlen(p) != 19 /*43*/) {
      continue;
    }
    
    while ( p && *p != '\0' && (i < 6/*14*/) ) {
      d[i] = strtol(p, &q, 16);
      if (!q) {
	break;
      }

      // 'd' array is the sequence of bytes
      p = q;
      if (g.debug) {
	fprintf(stdout, "d[%d]=[%02x] ", i, d[i]);   // dump bytes
      }
      i++;
    }
    
    if (g.debug) {
      printf("\n");
    }

    
    //
    // clamp meter logic
    //

    // convert bytes to 'reading' array
    reading[0] = d[1] << 8 | d[0];
    reading[1] = d[3] << 8 | d[2];
    reading[2] = d[5] << 8 | d[4];

    // Extract data items from first field
    function = (reading[0] >> 6) & 0x0f;
    scale = (reading[0] >> 3) & 0x07;
    decimal = reading[0] & 0x07;

    // Extract and convert measurement value (sign)
    if (reading[2] < 0x7fff) {
      measurement = reading[2];
    } else {
      measurement = -1 * (reading[2] & 0x7fff);
    }


    
    // debug
    if (g.debug) {
      printf("function=[%c%c%c%c%c%c%c%c], scale=[%02d], decimal=[%02d]\n", BYTE_TO_BINARY(function), scale, decimal);
      printf("  raw measurement=[%d]\n", measurement);
    }

    
    
    // convert a string of numerals to 'floating point' via dot insertion
    //  copy bytes over from src to dest until we get to where the 'dot' should go

    // string (integer) version of meter value
    sprintf(tmp_val_buf, "%04d", measurement);

    int tv = 0; // temp value
    int fv = 0; // fixed value
    memset(fixed_val_buf, 0, 16);

    for (tv = 0; tv < strlen(tmp_val_buf); tv++) {
      if ( tv == (strlen(tmp_val_buf) - decimal) ) {
	fixed_val_buf[fv++] = '.';
      }
      fixed_val_buf[fv++] = tmp_val_buf[tv];
    }

    
    // capture the timestamp
    char *ts = timestamp();

    
    // get the non-number part of the measurement
    char *funct_s = funct_to_string(fixed_val_buf, function, scale, measurement);

    
    // finally, print the value and any flags (if not in quiet-mode)
    if (!g.quiet) {
      if (!strcmp(fixed_val_buf, INF_OPEN_S)) {
	printf("%s %s\n", ts, funct_s);  // no value, just a 'flag'
      } else {
	printf("%s %s\n", ts, funct_s);
      }
    }

    
    // log to disk (with timestamps) if -l option was given
    if (fl) {
      if (!strcmp(fixed_val_buf, INF_OPEN_S)) {
	fprintf(fl, "%s %s\n", ts, funct_s);  // no value, just a 'flag'
      } else {
	fprintf(fl, "%s %s\n", ts, funct_s);
      }
    }

    if (g.debug) {
      printf("\n");
    }
    
  }

  
  if (pclose(fp)) {
    fprintf(stdout,"Command not found, or exited with error\n");

    if (fl) {
      fclose(fl);
    }
  }
  
  return 0;
}
