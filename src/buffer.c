/***********************************************************************/
/* buffer.c                                                            */
/* --------                                                            */
/*           GTKTerm Software                                          */
/*                      (c) Julien Schmitt                             */
/*                                                                     */
/* ------------------------------------------------------------------- */
/*                                                                     */
/*   Purpose                                                           */
/*      Management of a local buffer of data received                  */
/*                                                                     */
/*   ChangeLog                                                         */
/*      - 0.99.7 : removed (send)auto crlf stuff - (use macros instead)*/
/*      - 0.99.5 : Corrected segfault in case of buffer overlap        */
/*      - 0.99.2 : Internationalization                                */
/*      - 0.98.4 : file creation by Julien                             */
/*                                                                     */
/***********************************************************************/

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include "buffer.h"
#include "i18n.h"
#include "serial.h"

#include <config.h>
#include <glib/gi18n.h>
#include <time.h>

#define TIMESTAMP_SIZE 50

extern gboolean timestamp_on;
static int need_to_write_timestamp = 0;
static char *buffer = NULL;
static char *current_buffer;
static unsigned int pointer;
static int cr_received = 0;
char overlapped;

extern guint virt_col_pos;


void (*write_func)(const char *, unsigned int) = NULL;
void (*clear_func)(void) = NULL;

void create_buffer(void)
{
	if(buffer == NULL)
	{
		buffer = malloc(BUFFER_SIZE);
		clear_buffer();
	}
	return;
}

void delete_buffer(void)
{
	if(buffer != NULL)
		free(buffer);
	return;
}

//assumes that buffer always has space for timestamp (TIMESTAMP_SIZE)
//buffer points to location where timestamp will be inserted
unsigned int insert_timestamp(char *buffer)
{
	unsigned int size = 0;

	if (timestamp_on)
	{
		char buf[TIMESTAMP_SIZE];
		struct timespec ts;
		struct tm _tm;
		unsigned int nanos;
		timespec_get(&ts, TIME_UTC);
		nanos = ts.tv_nsec / 1000000;
		_tm = *localtime(&ts.tv_sec);
		snprintf(buf, TIMESTAMP_SIZE - 1, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] ",
				 1900 + _tm.tm_year, 1 + _tm.tm_mon, _tm.tm_mday,
				 _tm.tm_hour, _tm.tm_min, _tm.tm_sec, nanos);
		strcpy(buffer, buf);
		size = strlen(buf);
	}
	return size;
}

void put_chars(const char *chars, unsigned int size, gboolean crlf_auto)
{
	// buffer must still be valid after cr conversion or adding timestamp
	// only pointer is copied below
	char out_buffer[(BUFFER_RECEPTION*2) + TIMESTAMP_SIZE];
	const char *characters;

	/* If the auto CR LF mode on, read the buffer to add \r before \n */
	if(crlf_auto || timestamp_on)
	{
		int i, out_size = 0;

		for (i=0; i<size; i++)
		{
      if(crlf_auto)
			{
				if (chars[i] == '\r')
				{
					/* If the previous character was a CR too, insert a newline */
					if (cr_received)
					{
						out_buffer[out_size] = '\n';
						out_size++;
						need_to_write_timestamp = 1;
					}
					cr_received = 1;
				}
				else
				{
					if (chars[i] == '\n')
					{
						/* If we get a newline without a CR first, insert a CR */
						if (!cr_received)
						{
							out_buffer[out_size] = '\r';
							out_size++;
						}
					}
					else
					{
						/* If we receive a normal char, and the previous one was a
						   CR insert a newline */
						if (cr_received)
						{
							out_buffer[out_size] = '\n';
							out_size++;
							need_to_write_timestamp = 1;
						}
					}
					cr_received = 0;
				}
			} //if crlf_auto

			if(need_to_write_timestamp)
			{
				out_size += insert_timestamp(&out_buffer[out_size]);
				need_to_write_timestamp = 0;
			}

			if(chars[i] == '\n' )
			{
				need_to_write_timestamp = 1; //remember until we have a new character to print
			}

			//copy each character to new buffer
			out_buffer[out_size] = chars[i];
			out_size++; // increment for each stored character

		} // for

		// set "incomming" data pointer to new buffer containing all normal and
		// converted newline characters
		chars = out_buffer;
		size = out_size;
	} // if(crlf_auto || timestamp_on)

	if(buffer == NULL)
	{
		i18n_printf(_("ERROR : Buffer is not initialized !\n"));
		return;
	}

	// when incomming size is larger than buffer, then just print the
	// last BUFFER_SIZE characters and ignore all other at begin of buffer
	if(size > BUFFER_SIZE)
	{
		characters = chars + (size - BUFFER_SIZE);
		size = BUFFER_SIZE;
	}
	else
		characters = chars;

	if((size + pointer) >= BUFFER_SIZE)
	{
		memcpy(current_buffer, characters, BUFFER_SIZE - pointer);
		chars = characters + BUFFER_SIZE - pointer;
		pointer = size - (BUFFER_SIZE - pointer);
		memcpy(buffer, chars, pointer);
		current_buffer = buffer + pointer;
		overlapped = 1;
	}
	else
	{
		memcpy(current_buffer, characters, size);
		pointer += size;
		current_buffer += size;
	}

	if(write_func != NULL)
		write_func(characters, size);
}

void write_buffer(void)
{
	if(write_func == NULL)
		return;

	if(overlapped == 0)
		write_func(buffer, pointer);
	else
	{
		write_func(current_buffer, BUFFER_SIZE - pointer);
		write_func(buffer, pointer);
	}
}

void write_buffer_with_func(void (*func)(const char *, unsigned int))
{
	void (*write_func_backup)(const char *, unsigned int);

	write_func_backup = write_func;
	write_func = func;
	write_buffer();
	write_func = write_func_backup;
}

void clear_buffer(void)
{
	if(clear_func != NULL)
		clear_func();

	if(buffer == NULL)
		return;

	overlapped = 0;
	memset(buffer, 0, BUFFER_SIZE);
	current_buffer = buffer;
	pointer = 0;
	cr_received = 0;

	virt_col_pos = 0;
}

void set_clear_func(void (*func)(void))
{
	clear_func = func;
}

void unset_clear_func(void (*func)(void))
{
	clear_func = NULL;
}

void set_display_func(void (*func)(const char *, unsigned int))
{
	write_func = func;
}

void unset_display_func(void (*func)(const char *, unsigned int))
{
	write_func = NULL;
}
