/*
 MAX OLA Output Plugin
 OlaOutput.cpp
 
 Copyright (c) Daniel Edgar, Simon Newton and David Butler
 
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

#include <ola/BaseTypes.h>
#include <ola/Logging.h>
#include <ola/DmxBuffer.h>
#include <ola/StreamingClient.h>

using ola::StreamingClient;
using ola::DmxBuffer;

// object structure
typedef struct _olaoutput
{
	t_object object;
    
	void *outletDump;
    void *outletConnectionState;
    
	StreamingClient *client;
	DmxBuffer olaBuffer;
    
    t_uint8 buffer[512];
    
	t_atom_long universe;
    
    t_bool isBlackout;
	t_bool isConnected;
    
} t_olaoutput;


// prototypes
void *olaoutput_new(t_symbol *s, long argc, t_atom *argv);
void olaoutput_free(t_olaoutput* x);
void olaoutput_assist(t_olaoutput *x, void *b, long io, long index, char *s);

void olaoutput_list(t_olaoutput *x, t_symbol *msg, long argc, t_atom *argv);
void olaoutput_int(t_olaoutput *x, t_atom_long value);
void olaoutput_float(t_olaoutput *x, double dmxValue);
void olaoutput_channel(t_olaoutput *x, t_symbol *msg, long argc, t_atom *argv);

void olaoutput_connect(t_olaoutput* x);
void olaoutput_send(t_olaoutput *x);

t_max_err olaoutput_universe_set(t_olaoutput *x, t_object *attr, long argc, t_atom *argv);
t_max_err olaoutput_blackout_set(t_olaoutput *x, t_object *attr, long argc, t_atom *argv);

// The class structure for this plugin
static t_class *olaoutput_class = NULL;


// ******************************************************************************************************************

/*
 * The entry point to this plugin. This sets up the global s_ola_output_class
 * object which tells MAX how to create objects of type Ola Output.
 */
int C74_EXPORT main(void)
{
    // turn on OLA logging
    ola::InitLogging(ola::OLA_LOG_WARN, ola::OLA_LOG_STDERR);
    
    common_symbols_init();
	
    t_class *c = class_new("olaoutput",
                         (method) olaoutput_new,
                         (method) olaoutput_free,
                         sizeof(t_olaoutput),
                         (method) NULL,
                         A_GIMME,
                         0);
    
    class_addmethod(c, (method) olaoutput_list,      "list",     A_GIMME,    0);
    class_addmethod(c, (method) olaoutput_int,       "int",      A_LONG,     0);
    class_addmethod(c, (method) olaoutput_float,     "float",    A_FLOAT,    0);
    class_addmethod(c, (method) olaoutput_channel,   "channel",  A_GIMME,    0);
    class_addmethod(c, (method) olaoutput_connect,   "connect",              0);
    
    class_addmethod(c, (method) olaoutput_assist,    "assist",   A_CANT,     0);
    
    CLASS_ATTR_LONG(c, "universe", 0, t_olaoutput, universe);
    CLASS_ATTR_ACCESSORS(c, "universe", NULL, olaoutput_universe_set);
    CLASS_ATTR_LABEL(c, "universe", 0, "OLA Universe");
    CLASS_ATTR_CATEGORY(c, "universe", 0, "Settings");
    CLASS_ATTR_ORDER(c, "universe", 0, "1");
    CLASS_ATTR_SAVE(c, "universe", 0);
    
    CLASS_ATTR_CHAR(c, "blackout", 0, t_olaoutput, isBlackout);
    CLASS_ATTR_ACCESSORS(c, "blackout", NULL, olaoutput_blackout_set);
    CLASS_ATTR_STYLE_LABEL(c, "blackout", 0, "onoff", "Blackout");
    CLASS_ATTR_CATEGORY(c, "blackout", 0, "Settings");
    CLASS_ATTR_ORDER(c, "blackout", 0, "2");
    
    class_register(CLASS_BOX, c);
    olaoutput_class = c;
    
    post("olaoutput Version 0.9");

    return 0;
}

// ******************************************************************************************************************

/*
 * Create a new OlaOutput object.
 * @param t_symbol 
 * @param long number of arguments
 * @param argv argument atoms
 */
void *olaoutput_new(t_symbol *s, long argc, t_atom *argv)
{
    
	t_olaoutput *x = (t_olaoutput*) object_alloc(olaoutput_class);
	
	if (x) {
        
		x->outletDump = outlet_new((t_object*)x, NULL);
        x->outletConnectionState = outlet_new((t_object*)x, NULL);

        x->isConnected = false;
        x->isBlackout = false;
        
		// connect to ola
		x->client = new StreamingClient();
        
		if (argc > 0 && atom_gettype(argv) == A_LONG)
            object_attr_setlong(x, gensym("universe"), atom_getlong(argv));
        else
			x->universe = 0;
        
        attr_args_process(x, argc, argv);
	}
    
	return x;
}



/*
 * Cleanup this OlaOutput object
 * @param t_olaoutput the object to destroy
 */
void olaoutput_free(t_olaoutput *x)
{
    if (x->client) {
        delete x->client;
        x->client = NULL;
    }
}



/*
 * To show descriptions of your object’s inlets and outlets while editing a patcher,
 * this method can respond to the assist message with a function that copies the text to a string.
 * @param t_olaoutput a pointer to a t_olaoutput struct
 * @param b
 * @param io
 * @param index
 * @param s
 */
void olaoutput_assist(t_olaoutput *x, void *b, long io, long index, char *s)
{
	switch (io) {
		case 1:
            strncpy_zero(s, "olaoutput", 512);
            break;
		case 2:
            switch (index) {
                case 0:
                    strncpy_zero(s, "connection state", 512);
                    break;
                case 1:
                    strncpy_zero(s, "dump outlet", 512);
                    break;
            }
            break;
	}
}

// ******************************************************************************************************************

/*
 * Write a list of values to the local buffer
 * @param t_olaoutput a pointer to a t_olaoutput struct
 * @param msg the name of the message received
 * @param argc the number of atoms
 * @param argv a pointer to the head of a list of atoms
 */
void olaoutput_list(t_olaoutput *x, t_symbol *msg, long argc, t_atom *argv)
{
    t_atom_long dmxValue;
    
	for (int i = 0; i < argc && i < 512; i++) {
		
        switch (atom_gettype(argv+i)) {
            case A_LONG:
                dmxValue = atom_getlong(argv+i);
                dmxValue = std::min((t_atom_long)255, dmxValue);
                dmxValue = std::max((t_atom_long)0, dmxValue);
                x->buffer[i] = dmxValue;
                break;
                
            case A_FLOAT:
                dmxValue = (t_atom_long)floor(atom_getfloat(argv+i) + 0.5);
                dmxValue = std::min((t_atom_long)255, dmxValue);
                dmxValue = std::max((t_atom_long)0, dmxValue);
                x->buffer[i] = dmxValue;
                break;
                
            default:
                break;
        }
	}
    
    olaoutput_send(x);
}


/*
 * Writes a single int value to the local buffer
 * @param t_olaoutput a pointer to a t_olaoutput struct
 */
void olaoutput_int(t_olaoutput *x, t_atom_long dmxValue)
{
    dmxValue = std::min((t_atom_long)255, dmxValue);
    dmxValue = std::max((t_atom_long)0, dmxValue);
    x->buffer[0] = dmxValue;
    
    olaoutput_send(x);
}


/*
 * Write a single float value to the local buffer
 * @param t_olaoutput a pointer to a t_olaoutput struct
 */
void olaoutput_float(t_olaoutput *x, double dmxValue)
{
    olaoutput_int(x, (t_atom_long)floor(dmxValue + 0.5));
}



/*
 * Called to send data starting on a particular channel
 * @param t_olaoutput a pointer to a t_olaoutput struct
 * @param msg the name of the message received
 * @param argc the number of atoms
 * @param argv a pointer to the head of a list of atoms
 */
void olaoutput_channel(t_olaoutput *x, t_symbol *msg, long argc, t_atom *argv)
{
    if (atom_gettype(argv) == A_LONG) {
        
        t_atom_long channel = atom_getlong(argv);
        
        if (channel >= 1 && channel <= 512) {
            
            t_atom_long dmxValue;
            
            for (int i = 1; i < argc && i < 513; i++) {
                
                switch (atom_gettype(argv+i)) {
                        
                    case A_LONG:
                        dmxValue = atom_getlong(argv+i);
                        dmxValue = std::min((t_atom_long)255, dmxValue);
                        dmxValue = std::max((t_atom_long)0, dmxValue);
                        x->buffer[i+(channel-1)] = dmxValue;
                        break;
                        
                    case A_FLOAT:
                        dmxValue = (t_atom_long)floor(atom_getfloat(argv+i) + 0.5);
                        dmxValue = std::min((t_atom_long)255, dmxValue);
                        dmxValue = std::max((t_atom_long)0, dmxValue);
                        x->buffer[i+(channel-1)] = dmxValue;
                        break;
                        
                    default:
                        break;
                }
            }
            
            olaoutput_send(x);
            return;
            
        } else {
            object_error((t_object*)x, "Channel must be between 1 and 512 inclusive");
            return;
        }
        
    } else {
        object_error((t_object*)x, "Channel must be an integer");
        return;
    }
    
}
// ******************************************************************************************************************

/*
 * Initiates the connection to OLA
 * @param t_olaoutput a pointer to a t_olaoutput struct
 */
void olaoutput_connect(t_olaoutput *x)
{
    if (x->isConnected == false || x->client == NULL) {
    
        //initiate the connection to ola
        if (x->client->Setup()) {
            
            x->isConnected = true;
            object_post((t_object*)x, "Connected to OLA");
            
        } else {
            
            x->isConnected = false;
            object_post((t_object*)x, "OLA connection failed. olad must be running to send data.");
            
        }
    }
    
    outlet_int(x->outletConnectionState, x->isConnected);
}


/*
 * Sends data in the local buffer to OLA
 * @param t_olaoutput a pointer to a t_olaoutput struct
 */
void olaoutput_send(t_olaoutput *x)
{
    if (x->isConnected == false)
        olaoutput_connect(x);
    
    if (x->isBlackout)
        x->olaBuffer.Blackout();
    else
        x->olaBuffer.Set(x->buffer, 512);
    
    if (!x->client->SendDmx(x->universe, x->olaBuffer)) {
        
        if (x->isConnected) {
            x->isConnected = false;
            outlet_int(x->outletConnectionState, x->isConnected);
            object_error((t_object*)x, "Connection to OLA lost");
        } else {
            object_error((t_object*)x, "Not connected to OLA");
        }
    }
}

// ******************************************************************************************************************

/*
 * Sets the value of the universe attribute
 * @param t_olaoutput a pointer to a t_olaoutput struct
 * @param attr
 * @param argc
 * @param argv
 */
t_max_err olaoutput_universe_set(t_olaoutput *x, t_object *attr, long argc, t_atom *argv)
{
    t_atom_long universe = atom_getlong(argv);
        
    if (universe >= 0 && universe <= 4294967295) {
        x->universe = universe;
        olaoutput_send(x);
    }
    
    return MAX_ERR_NONE;
}



/*
 * Sets the value of the blackout attribute
 * @param t_olaoutput a pointer to a t_olaoutput struct
 * @param attr
 * @param argc
 * @param argv
 */
t_max_err olaoutput_blackout_set(t_olaoutput *x, t_object *attr, long argc, t_atom *argv)
{
    t_atom_long blackout = atom_getlong(argv);
        
    if (blackout > 0)
        x->isBlackout = true;
    else
        x->isBlackout = false;
    
    olaoutput_send(x);
    
    return MAX_ERR_NONE;
}



  
