/*
 *  Player - One Hell of a Robot Server
 *  Copyright (C) 2005 -
 *     Brian Gerkey
 *                      
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/** @ingroup utils */
/** @{ */
/** @defgroup util_player player server
 @brief TCP server that allows remote access to Player devices.

The most commonly-used of the Player utilities, @b player is a TCP
server that allows remote access to devices.  It is normally executed
on-board a robot, and is given a configuration file that maps the robot's
hardware to Player devices, which are then accessible to client programs.
The situation is essentially the same when running a simulation.

@section Usage

@code
player [-q] [-d <level>] [-p <port>] [-h] <cfgfile>
@endcode
Arguments:
- -h : Give help info; also lists drivers that were compiled into the server.
- -q : Quiet startup
- -d \<level\> : Set the debug level, 0-9.  0 is only errors, 1 is errors +
warnings, 2 is errors + warnings + diagnostics, higher levels may give
more output.  Default: 1.
- -p \<port\> : Establish the default TCP port, which will be assigned to
any devices in the configuration file without an explicit port assignment.
Default: 6665.
- \<cfgfile\> : The configuration file to read.

@section Example

@code
$ player pioneer.cfg

* Part of the Player/Stage/Gazebo Project
* [http://playerstage.sourceforge.net].
* Copyright (C) 2000 - 2006 Brian Gerkey, Richard Vaughan, Andrew Howard,
* Nate Koenig, and contributors. Released under the GNU General Public
* License.
* Player comes with ABSOLUTELY NO WARRANTY.  This is free software, and you
* are welcome to redistribute it under certain conditions; see COPYING
* for details.

Listening on ports: 6665
@endcode

*/
/** @} */

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>

#include <libplayercore/playercore.h>
#include <libplayertcp/playertcp.h>
#include <libplayerxdr/functiontable.h>
#include <libplayerdrivers/driverregistry.h>


void PrintCopyrightMsg();
void PrintUsage();
int ParseArgs(int* port, int* debuglevel, 
              char** cfgfilename, int* gz_serverid,
              int argc, char** argv);
void Quit(int signum);
void Cleanup();


// TODO:
// RTV - some drivers need access to the cmdline args, e.g. for X or GTK
// options. Need to think about the argument parsing strategy


PlayerTCP* ptcp;
ConfigFile* cf;

int
main(int argc, char** argv)
{
  int debuglevel = 1;
  int port = PLAYERTCP_DEFAULT_PORT;
  int gz_serverid = -1;
  int* ports;
  int num_ports;
  char* cfgfilename;

  if(signal(SIGINT, Quit) == SIG_ERR)
  {
    PLAYER_ERROR1("signal() failed: %s", strerror(errno));
    exit(-1);
  }

  if(signal(SIGPIPE, SIG_IGN) == SIG_ERR)
  {
    PLAYER_ERROR1("signal() failed: %s", strerror(errno));
    exit(-1);
  }
  
  player_globals_init();
  player_register_drivers();
  playerxdr_ftable_init();

  ptcp = new PlayerTCP();
  assert(ptcp);

  if(ParseArgs(&port, &debuglevel, &cfgfilename, &gz_serverid, argc, argv) < 0)
  {
    PrintUsage();
    exit(-1);
  }

  ErrorInit(debuglevel);

  PrintCopyrightMsg();

  cf = new ConfigFile("localhost",port);
  assert(cf);

  if(!cf->Load(cfgfilename))
  {
    PLAYER_ERROR1("failed to load config file %s", cfgfilename);
    exit(-1);
  }

  if(!cf->ParseAllDrivers())
  {
    PLAYER_ERROR1("failed to parse config file %s", cfgfilename);
    exit(-1);
  }

  cf->WarnUnused();

  if(deviceTable->StartAlwaysonDrivers() != 0)
  {
    PLAYER_ERROR("failed to start alwayson drivers");
    Cleanup();
    exit(-1);
  }

  // Collect the list of ports on which we should listen
  ports = (int*)calloc(deviceTable->Size(),sizeof(int));
  assert(ports);
  num_ports = 0;
  for(Device* device = deviceTable->GetFirstDevice();
      device;
      device = deviceTable->GetNextDevice(device))
  {
    // don't listen locally for remote devices
    if(!strcmp(device->drivername,"remote"))
      continue;
    int i;
    for(i=0;i<num_ports;i++)
    {
      if((int)device->addr.robot == ports[i])
        break;
    }
    if(i==num_ports)
      ports[num_ports++] = device->addr.robot;
  }

  if(ptcp->Listen(ports, num_ports) < 0)
  {
    PLAYER_ERROR("failed to listen on requested ports");
    Cleanup();
    exit(-1);
  }

  printf("Listening on ports: ");
  for(int i=0;i<num_ports;i++)
    printf("%d ", ports[i]);
  puts("");

  free(ports);

  while(!player_quit)
  {
    if(ptcp->Accept(0) < 0)
    {
      PLAYER_ERROR("failed while accepting new connections");
      break;
    }

    if(ptcp->Read(1) < 0)
    {
      PLAYER_ERROR("failed while reading");
      break;
    }

    deviceTable->UpdateDevices();

    if(ptcp->Write() < 0)
    {
      PLAYER_ERROR("failed while reading");
      break;
    }
  }

  puts("Quitting.");

  Cleanup();

  return(0);
}

void
Cleanup()
{
  delete ptcp;
  player_globals_fini();
  delete cf;
}

void
Quit(int signum)
{
  player_quit = true;
}

void
PrintCopyrightMsg()
{
  fprintf(stderr,"\n* Part of the Player/Stage/Gazebo Project [http://playerstage.sourceforge.net].\n");
  fprintf(stderr, "* Copyright (C) 2000 - 2006 Brian Gerkey, Richard Vaughan, Andrew Howard,\n* Nate Koenig, and contributors.");
  fprintf(stderr," Released under the GNU General Public License.\n");
  fprintf(stderr,"* Player comes with ABSOLUTELY NO WARRANTY.  This is free software, and you\n* are welcome to redistribute it under certain conditions; see COPYING\n* for details.\n\n");
}

void 
PrintUsage()
{
  int maxlen=66;
  char** sortedlist;

  fprintf(stderr, "USAGE:  player [options] [<configfile>]\n\n");
  fprintf(stderr, "Where [options] can be:\n");
  fprintf(stderr, "  -h             : print this message.\n");
  fprintf(stderr, "  -d <level>     : debug message level (0 = none, 1 = default, 9 = all).\n");
  fprintf(stderr, "  -p <port>      : port where Player will listen. "
          "Default: %d\n", PLAYERTCP_DEFAULT_PORT);
  fprintf(stderr, "  -q             : quiet mode: minimizes the console output on startup.\n");
  fprintf(stderr, "  <configfile>   : load the the indicated config file\n");
  fprintf(stderr, "\nThe following %d drivers were compiled into Player:\n\n    ",
          driverTable->Size());
  sortedlist = driverTable->SortDrivers();
  for(int i=0, len=0; i<driverTable->Size(); i++)
  {
    if((len += strlen(sortedlist[i])) >= maxlen)
    {
      fprintf(stderr,"\n    ");
      len=strlen(sortedlist[i]);
    }
    fprintf(stderr, "%s ", sortedlist[i]);
  }
  free(sortedlist);
  fprintf(stderr, "\n\n");
}


int 
ParseArgs(int* port, int* debuglevel, char** cfgfilename, int* gz_serverid,
          int argc, char** argv)
{
  int ch;
  const char* optflags = "d:p:hq";
  
  // Get letter options
  while((ch = getopt(argc, argv, optflags)) != -1)
  {
    switch (ch)
    {
      case 'q':
        player_quiet_startup = true;
        break;
      case 'd':
        *debuglevel = atoi(optarg);
        break;
      case 'p':
        *port = atoi(optarg);
        break;
      case '?':
      case ':':
      case 'h':	
      default:
        return(-1);
    }
  }

  if(optind >= argc)
    return(-1);
  
  *cfgfilename = argv[optind];

  return(0);
}