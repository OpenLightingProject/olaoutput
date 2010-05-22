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
#include <ola/Logging.h>
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


enum ConnectionState {	
	disconnected = 0,   	
	connected = 1
};


// The OLA Output object.
typedef struct  {
	t_object c_box;
	void *m_outlet1;
	StreamingClient *client;
	DmxBuffer buffer;
	long universe;
	enum ConnectionState connectionState;
} t_ola_output;


// prototypes
void *ola_output_new(t_symbol *s, long argc, t_atom *argv);
void ola_output_free(t_ola_output* ola_output);
void ola_output_list(t_ola_output *ola_output, t_symbol *msg, long argc, t_atom *argv);
void ola_output_int(t_ola_output *ola_output, long value);
void ola_output_assist(t_ola_output *ola_output, void *b, long io, long index, char *s);
void ola_output_blackout(t_ola_output* ola_output);
void ola_output_state(t_ola_output* ola_output);
void ola_output_connect(t_ola_output* ola_output);
void ola_output_in1(t_ola_output *ola_output, long n);
void ola_output_outlet(t_ola_output *ola_output);
void setConnectionState(t_ola_output *ola_output, ConnectionState state);
void SetOlaStateDisconnected(t_ola_output *ola_output);
void SetupOlaConnection(t_ola_output *ola_output);



// The class structure for this plugin
static t_class *s_ola_output_class = NULL;


/*
 * The entry point to this plugin. This sets up the global s_ola_output_class
 * object which tells MAX how to create objects of type Ola Output.
 */
int T_EXPORT main(void) {
  
  // turn on OLA logging
  ola::InitLogging(ola::OLA_LOG_WARN, ola::OLA_LOG_STDERR);
	
	
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
  class_addmethod(c, (method) ola_output_state, "state", 0);
  class_addmethod(c, (method) ola_output_connect, "connect", 0);
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
		//set up connectionstate outlet
		ola_output->m_outlet1 = intout((t_object *)ola_output); 

		//set up ola
		ola_output->client = new StreamingClient();

		//set up the connection to ola
		SetupOlaConnection(ola_output);
		
		
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
		if (!ola_output->client->SendDmx(ola_output->universe, ola_output->buffer)) {
			//looks like ola is disconnected
			SetOlaStateDisconnected(ola_output);
		}	
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
		if (!ola_output->client->SendDmx(ola_output->universe, ola_output->buffer)) {
			//looks like ola is disconnected
			SetOlaStateDisconnected(ola_output);
		}	
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
			strncpy_zero(s, "Status messages are sent to this outlet", 512); 
			break;
	}
}


/*
 * Sends a blackout message to OLA 
 * @param ola_output a pointer to a t_ola_output struct
 */
void ola_output_blackout(t_ola_output *ola_output) {
	
	if (ola_output->client) {
		ola_output->buffer.Blackout();
		if (!ola_output->client->SendDmx(ola_output->universe, ola_output->buffer)) {
			//looks like ola is disconnected
			SetOlaStateDisconnected(ola_output);
		}
		
	}
}


/*
 * Sends the current OLA connection state to max 
 * @param ola_output a pointer to a t_ola_output struct
 */
void ola_output_state(t_ola_output *ola_output) {
	
	//output the current connection state value to max
	ola_output_outlet(ola_output);
}


/*
 * Initiates the connection to OLA, in case olaoutput didn't connect on instantiation 
 * @param ola_output a pointer to a t_ola_output struct
 */
void ola_output_connect(t_ola_output *ola_output) {
	
	if (ola_output->connectionState == disconnected) {
		
		//set up the connection to ola
		SetupOlaConnection(ola_output);
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


/*
 * Outputs OLA connection status
 * 0 = not connected to ola, 1 = connected to ola
 * @param ola_output a pointer to a t_ola_output struct
 */
void ola_output_outlet(t_ola_output *ola_output) {
	outlet_int(ola_output->m_outlet1, (long)ola_output->connectionState);
}





/* helper methods */

void setConnectionState(t_ola_output *ola_output, ConnectionState state) {
	
	//set the new state
	ola_output->connectionState = state;
	
	//and output the current value to max
	ola_output_outlet(ola_output);
	
}


/*
 * This we call if we are unable to successfully send dmx messages to ola
 */
void SetOlaStateDisconnected(t_ola_output *ola_output) {
	post("OLA StreamingClient: disconnected from olad");
	setConnectionState(ola_output, disconnected); //hmm, not connected, so output the current state
}


void SetupOlaConnection(t_ola_output *ola_output) {
	//initiate the connection to ola
	if (!ola_output->client->Setup()) {
		setConnectionState(ola_output, disconnected); //hmm, not connected, so output the current state
		post("OLA StreamingClient setup failed. Is olad running?");
	}
	else {
		setConnectionState(ola_output, connected); //looks like we've connected, so output the current state
		post("OLA StreamingClient setup successful. Connected to olad.");
	}
}	

  
