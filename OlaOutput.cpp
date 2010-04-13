/*
 MAX OLA Output Plugin
 OlaOutput.cpp
 
 Copyright (c) Daniel Edgar and Simon Newton
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 Version 2 as published by the Free Software Foundation.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details. The license is
 in the file "COPYING".
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/**
 @file
 OlaOutput - Sends MAX list data to the OLA Daemon
 
 @ingroup  examples
 */

#include "ext.h"
#include "ext_obex.h"
#include "ext_strings.h"
#include "ext_common.h"
#include "ext_systhread.h"
#include <ola/BaseTypes.h>
#include <ola/DmxBuffer.h>
#include <ola/StreamingClient.h>
#include <algorithm>

using ola::StreamingClient;
using ola::DmxBuffer;

// a macro to mark exported symbols in the code without requiring an external file to define them
#ifdef WIN_VERSION
  // note that this the required syntax on windows regardless of whether the compiler is msvc or gcc
  #define T_EXPORT __declspec(dllexport)
#else // MAC_VERSION
  // the mac uses the standard gcc syntax, you should also set the -fvisibility=hidden flag to hide the non-marked symbols
  #define T_EXPORT __attribute__((visibility("default")))
#endif


// The OLA Output object.
typedef struct  {
	t_object c_box;
	StreamingClient *client;
	DmxBuffer buffer;
	long universe;
} t_ola_output;


// prototypes
void *ola_output_new(t_symbol *s, long argc, t_atom *argv);
void ola_output_free(t_ola_output* x);
void ola_output_list(t_ola_output *x, t_symbol *msg, long argc, t_atom *argv);
void ola_output_int(t_ola_output *x, long value);
void ola_output_assist(t_ola_output *ola_output, void *b, long io, long index, char *s);
void ola_output_blackout(t_ola_output* x);
void ola_output_in1(t_ola_output *ola_output, long n);


// The class structure for this plugin
static t_class *s_ola_output_class = NULL;


/*
 * The entry point to this plugin. This sets up the global s_ola_output_class
 * object which tells MAX how to create objects of type Ola Output.
 */
int T_EXPORT main(void) {
  t_class *c = class_new("olaoutput",
                         (method) ola_output_new,
                         (method) ola_output_free,
                         sizeof(t_ola_output),
                         (method) NULL,
                         A_GIMME,
                         0);

  common_symbols_init();
  class_addmethod(c, (method) ola_output_list, "list", A_GIMME, 0);
  class_addmethod(c, (method) ola_output_int, "int", A_LONG, 0);
  class_addmethod(c, (method) ola_output_assist, "assist", A_CANT, 0);
  class_addmethod(c, (method) ola_output_blackout, "blackout", 0);
  class_addmethod(c, (method) ola_output_in1, "in1", A_LONG, 0);
  class_register(CLASS_BOX, c);
  s_ola_output_class = c;
  return 0;
}


/*
 * Create a new OlaOutput object.
 * @param s?
 */
void *ola_output_new(t_symbol *s, long argc, t_atom *argv) {
	t_ola_output *ola_output = (t_ola_output*) object_alloc(s_ola_output_class);
	if (ola_output) {
		ola_output->client = new StreamingClient();
		if (!ola_output->client->Setup()) {
			post("OlaOutput: OLA StreamingClient setup failed. Is olad running?");
		}
		if (argc == 1 && atom_getlong(argv+0) != 0) { //the first argument is universe
			ola_output->universe = atom_getlong(argv+0); //if specified, use it
		}
		else {
			ola_output->universe = 1; //otherwise default the universe to 1
		}

		intin(ola_output, 1); //create the universe inlet
	}
	return ola_output;
}


/*
 * Cleanup this OlaOutput object
 * @param ola_output the object to destroy
 */
void ola_output_free(t_ola_output *ola_output) {
  if (ola_output->client) {
    delete ola_output->client;
    ola_output->client = NULL;
  }
}


/*
 * Called when we get list data
 * @param ola_output a pointer to a t_ola_output struct
 * @param msg the name of the message received
 * @param argc the number of atoms
 * @param argv a pointer to the head of a list of atoms
 */
void ola_output_list(t_ola_output *ola_output, t_symbol *msg, long argc, t_atom *argv) {

	for (int i = 0; i < argc; i++) {
		
		long dmxVal = atom_getlong(argv+i); //atom_getlong returns only integers, truncates floats. See Max SDK for type coercion scenarios.

		//bounds checking and limiting
		dmxVal = std::min((long)255, dmxVal);
		dmxVal = std::max((long)0, dmxVal);
		
		ola_output->buffer.SetChannel(i, dmxVal); 
	
	}

	if (ola_output->client) {
		ola_output->client->SendDmx(ola_output->universe, ola_output->buffer);
	}
}

/*
 * Called when we get a single int value
 * Sends out specified DMX value to channel
 * @param ola_output a pointer to a t_ola_output struct
 * @param msg the name of the message received
 * @param argc the number of atoms
 * @param argv a pointer to the head of a list of atoms
 */
void ola_output_int(t_ola_output *ola_output, long value)
{
	long dmxVal = value;
	//bounds checking and limiting
	dmxVal = std::min((long)255, dmxVal);
	dmxVal = std::max((long)0, dmxVal);
		
	ola_output->buffer.SetChannel(0, dmxVal); 
		
	if (ola_output->client) {
		ola_output->client->SendDmx(ola_output->universe, ola_output->buffer);
	}
}


/*
 * To show descriptions of your object’s inlets and outlets while editing a patcher, 
 * this method can respond to the assist message with a function that copies the text to a string.
 * @param ola_output a pointer to a t_ola_output struct
 */
void ola_output_assist(t_ola_output *ola_output, void *b, long io, long index, char *s) {

	switch (io) { 
		case 1:
			switch (index) { 
				case 0:
					strncpy_zero(s, "The primary inlet to receive messages", 512);
					break; 
				case 1:
					strncpy_zero(s, "Which dmx universe to send messages to (defaults to 1)", 512);
					break;
			}
			break; 
		case 2:
			strncpy_zero(s, "This is a description of the outlet", 512); 
			break;
	}
}


/*
 * Sends a blackout message to OLA 
 * @param ola_output a pointer to a t_ola_output struct
 */
void ola_output_blackout(t_ola_output *ola_output) {
	
	post("OlaOutput: sending blackout to OLA");
	
	if (ola_output->client) {
		ola_output->buffer.Blackout();
		ola_output->client->SendDmx(ola_output->universe, ola_output->buffer);
	}
}



/*
 * Sets which universe OLA should send dmx messages to 
 * @param ola_output a pointer to a t_ola_output struct
 * @param n the universe number
 */
void ola_output_in1(t_ola_output *ola_output, long n) {
	ola_output->universe = n;
}
  
