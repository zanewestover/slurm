/*****************************************************************************\
 * controller.c - main control machine daemon for slurm
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette@llnl.gov>, Kevin Tew <tew1@llnl.gov> et. al.
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <src/common/pack.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/macros.h>
#include <src/slurmctld/slurmctld.h>

#define BUF_SIZE 1024

time_t init_time;
slurm_ctl_conf_t slurmctld_conf;

int msg_from_root (void);
void slurmctld_req ( slurm_msg_t * msg );
void fill_ctld_conf ( slurm_ctl_conf_t * build_ptr );
void parse_commandline( int argc, char* argv[], slurm_ctl_conf_t * );
inline static void slurm_rpc_dump_build ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_nodes ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_partitions ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_dump_jobs ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_cancel ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_submit_batch_job ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_reconfigure_controller ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_node_registration ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_register_node_status ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_update_job ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_update_node ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_update_partition ( slurm_msg_t * msg ) ;
inline static void slurm_rpc_job_will_run ( slurm_msg_t * msg ) ;

inline static void slurm_rpc_allocate_resources ( slurm_msg_t * msg , uint8_t immediate ) ;


int 
main (int argc, char *argv[]) 
{
	int error_code ;
	slurm_fd newsockfd;
        slurm_fd sockfd;
	slurm_msg_t * msg = NULL ;
	slurm_addr cli_addr ;
	char node_name[MAX_NAME_LEN];
	log_options_t opts = LOG_OPTS_STDERR_ONLY ;

	init_time = time (NULL);
	log_init(argv[0], opts, SYSLOG_FACILITY_DAEMON, NULL);

	fill_ctld_conf ( &slurmctld_conf );
	parse_commandline ( argc, argv, &slurmctld_conf );

	if ( ( error_code = init_slurm_conf () ) ) 
		fatal ("slurmctld: init_slurm_conf error %d", error_code);
	if ( ( error_code = read_slurm_conf ( slurmctld_conf.slurm_conf )) ) 
		fatal ("slurmctld: error %d from read_slurm_conf reading %s", error_code, SLURM_CONF);
	if ( ( error_code = gethostname (node_name, MAX_NAME_LEN) ) ) 
		fatal ("slurmctld: errno %d from gethostname", errno);

	if ( strcmp (node_name, slurmctld_conf.control_machine) &&  
	     strcmp (node_name, slurmctld_conf.backup_machine) )
	       	fatal ("slurmctld: this machine (%s) is not the primary (%s) or backup (%s) controller", 
			node_name, slurmctld_conf.control_machine, slurmctld_conf.backup_machine);
	
	if ( ( sockfd = slurm_init_msg_engine_port ( SLURM_PORT ) ) == SLURM_SOCKET_ERROR )
		fatal ("slurmctld: error starting message engine \n", errno);
		
	while (1) 
	{
		/* accept needed for stream implementation 
		 * is a no-op in message implementation that just passes sockfd to newsockfd
		 */
		if ( ( newsockfd = slurm_accept_msg_conn ( sockfd , & cli_addr ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurmctld: error %d from connect", errno) ;
			break ;
		}
		
		/* receive message call that must occur before thread spawn because in message 
		 * implementation their is no connection and the message is the sign of a new connection */
		msg = xmalloc ( sizeof ( slurm_msg_t ) ) ;	
		if (msg == NULL)
			return ENOMEM;
		
		if ( ( error_code = slurm_receive_msg ( newsockfd , msg ) ) == SLURM_SOCKET_ERROR )
		{
			error ("slurmctld: error %d from accept", errno);
			break ;
		}

		msg -> conn_fd = newsockfd ;	
/************************* 
 * convert to pthread, tbd 
 *************************/
		slurmctld_req ( msg );	/* process the request */
		/* close should only be called when the stream implementation is being used 
		 * the following call will be a no-op in the message implementation */
		slurm_close_accepted_conn ( newsockfd ); /* close the new socket */
	}			
	return 0 ;
}

void
slurmctld_req ( slurm_msg_t * msg )
{
	
	switch ( msg->msg_type )
	{	
		case REQUEST_BUILD_INFO:
			slurm_rpc_dump_build ( msg ) ;
			slurm_free_last_update_msg ( msg -> data ) ;
			break;
		case REQUEST_NODE_INFO:
			slurm_rpc_dump_nodes ( msg ) ;
			slurm_free_last_update_msg ( msg -> data ) ;
			break ;
		case REQUEST_JOB_INFO:
			slurm_rpc_dump_jobs ( msg ) ;
			slurm_free_last_update_msg ( msg -> data ) ;
			break;
		case REQUEST_PARTITION_INFO:
			slurm_rpc_dump_partitions ( msg ) ;
			slurm_free_last_update_msg ( msg -> data ) ;
			break;
		case REQUEST_RESOURCE_ALLOCATION :
			slurm_rpc_allocate_resources ( msg, false ) ;
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_IMMEDIATE_RESOURCE_ALLOCATION :
			slurm_rpc_allocate_resources ( msg, true ) ;
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_JOB_WILL_RUN :
			slurm_rpc_job_will_run ( msg -> data ) ;
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_CANCEL_JOB:
			slurm_rpc_job_cancel ( msg ) ;
			slurm_free_job_id_msg ( msg -> data ) ;
			break;
		case REQUEST_SUBMIT_BATCH_JOB: 
			slurm_rpc_submit_batch_job ( msg ) ;
			slurm_free_job_desc_msg ( msg -> data ) ; 
			break;
		case MESSAGE_NODE_REGISTRATION_STATUS:
			slurm_rpc_register_node_status ( msg ) ;
			slurm_free_node_registration_status_msg ( msg -> data ) ;
			break;
		case REQUEST_RECONFIGURE:
			slurm_rpc_reconfigure_controller ( msg ) ;
			break;
		case REQUEST_UPDATE_JOB:
			slurm_rpc_update_job ( msg ) ;
			slurm_free_job_desc_msg ( msg -> data ) ;
			break;
		case REQUEST_UPDATE_NODE:
			slurm_rpc_update_node ( msg ) ;
			break;
		case REQUEST_UPDATE_PARTITION:
			slurm_rpc_update_partition ( msg ) ;
			break;
		default:
			error ("slurmctld_req: invalid request msg type %d\n", msg-> msg_type);
			slurm_send_rc_msg ( msg , EINVAL );
			break;
	}
	slurm_free_msg ( msg ) ;
}


void
slurm_rpc_dump_build ( slurm_msg_t * msg )
{
	clock_t start_time;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	slurm_ctl_conf_info_msg_t build_tbl ;

	start_time = clock ();
	
	
	/* check to see if build_data has changed */	
	if ( last_time_msg -> last_update >= init_time )
	{
		info ("slurmctld_req: dump_build, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		/* success */
		fill_ctld_conf ( & build_tbl ) ;
		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_BUILD_INFO ;
		response_msg . data = & build_tbl ;

		/* send message */
		info ("slurmctld_req: dump_build time=%ld", (long) (clock () - start_time));
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
	}
}

void
slurm_rpc_dump_jobs ( slurm_msg_t * msg )
{
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	time_t last_update = last_time_msg -> last_update ;
	

	start_time = clock ();

	if ( last_time_msg -> last_update >= last_job_update )
	{
		info ("slurmctld_req: dump_job, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		/* success */
		pack_all_jobs (&dump, &dump_size, &last_update);
		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_JOB_INFO ;
		response_msg . data = dump ;
		response_msg . data_size = dump_size ;

		/* send message */
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
		info ("slurmctld_req: dump_job, size=%d, time=%ld", 
		      dump_size, (long) (clock () - start_time));
		if (dump)
			xfree (dump);
	}
}

void
slurm_rpc_dump_nodes ( slurm_msg_t * msg )
{
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	time_t last_update = last_time_msg -> last_update ;

	start_time = clock ();

	if ( last_time_msg -> last_update >= last_node_update )
	{
		info ("slurmctld_req: dump_node, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		/* success */
		pack_all_node (&dump, &dump_size, &last_update);
		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_NODE_INFO ;
		response_msg . data = dump ;
		response_msg . data_size = dump_size ;

		/* send message */
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
		info ("slurmctld_req: dump_node, size=%d, time=%ld", 
		      dump_size, (long) (clock () - start_time));
		if (dump)
			xfree (dump);
	}
}

void
slurm_rpc_dump_partitions ( slurm_msg_t * msg )
{
	clock_t start_time;
	char *dump;
	int dump_size;
	slurm_msg_t response_msg ;
	last_update_msg_t * last_time_msg = ( last_update_msg_t * ) msg-> data ;
	time_t last_update = last_time_msg -> last_update ;

	start_time = clock ();

	if ( last_time_msg -> last_update >= last_part_update )
	{
		info ("slurmctld_req: dump_part, no change, time=%ld", 
			(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_NO_CHANGE_IN_DATA );
	}
	else
	{
		/* success */
		pack_all_part (&dump, &dump_size, &last_update);
		/* init response_msg structure */
		response_msg . address = msg -> address ;
		response_msg . msg_type = RESPONSE_PARTITION_INFO ;
		response_msg . data = dump ;
		response_msg . data_size = dump_size ;

		/* send message */
		slurm_send_node_msg( msg -> conn_fd , &response_msg ) ;
		info ("slurmctld_req: dump_part, size=%d, time=%ld", 
		      dump_size, (long) (clock () - start_time));
		if (dump)
			xfree (dump);
	}
}

void 
slurm_rpc_job_cancel ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	job_id_msg_t * job_id_msg = ( job_id_msg_t * ) msg-> data ;

	start_time = clock ();

	/* do RPC call */
	error_code = job_cancel ( job_id_msg->job_id );

	/* return result */
	if (error_code)
	{
		info ("slurmctld_req: job_cancel error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: job_cancel success for %d, time=%ld",
				job_id_msg->job_id, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

}

void 
slurm_rpc_update_job ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;

	start_time = clock ();
		
	/* do RPC call */
	error_code = update_job ( job_desc_msg );

	/* return result */
	if (error_code)
	{
		error ("slurmctld_req: update error %d on job id %u, time=%ld",
				error_code, job_desc_msg->job_id, 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: updated job id %u, time=%ld",
				job_desc_msg->job_id, 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}
}

void 
slurm_rpc_update_node ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	update_node_msg_t * update_node_msg_ptr ;
	start_time = clock ();
	
	update_node_msg_ptr = (update_node_msg_t * ) msg-> data ;
	
	/* do RPC call */
	error_code = update_node ( update_node_msg_ptr );

	/* return result */
	if (error_code)
	{
		error ("slurmctld_req: update error %d on node %s, time=%ld",
				error_code, update_node_msg_ptr->node_names, 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: updated node %s, time=%ld",
				update_node_msg_ptr->node_names, 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}
	if (update_node_msg_ptr->node_names)
		xfree (update_node_msg_ptr->node_names);
}

void 
slurm_rpc_update_partition ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	update_part_msg_t * part_desc_ptr = (update_part_msg_t * ) msg-> data ;
	start_time = clock ();

	/* do RPC call */
	error_code = update_part ( part_desc_ptr );

	/* return result */
	if (error_code)
	{
		error ("slurmctld_req: update error %d on partition %s, time=%ld",
				error_code, part_desc_ptr->name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: updated partition %s, time=%ld",
				part_desc_ptr->name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}
	if (part_desc_ptr->name)
		xfree (part_desc_ptr->name);
	if (part_desc_ptr->nodes)
		xfree (part_desc_ptr->nodes);
	if (part_desc_ptr->allow_groups)
		xfree (part_desc_ptr->allow_groups);
}

void
slurm_rpc_submit_batch_job ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	struct job_record *job_rec_ptr;
	uint32_t job_id ;
	slurm_msg_t response_msg ;
	submit_response_msg_t submit_msg ;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;

	start_time = clock ();

	/* do RPC call */
	dump_job_desc(job_desc_msg);
	error_code = job_create(job_desc_msg, &job_id, 0, 0, &job_rec_ptr);

	/* return result */
	if (error_code)
	{
		info ("slurmctld_req: job_submit error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: job_submit success for id=%u, time=%ld",
				job_id, (long) (clock () - start_time));
		/* send job_ID */
		submit_msg . job_id = job_id ;
		response_msg . msg_type = RESPONSE_SUBMIT_BATCH_JOB ;
		response_msg . data = & submit_msg ;
		slurm_send_node_msg ( msg->conn_fd , & response_msg ) ;
	}
	schedule();
}

/* slurm_rpc_allocate_resources:  allocate resources for a job */
void slurm_rpc_allocate_resources ( slurm_msg_t * msg , uint8_t immediate )
{
	/* init */
	int error_code;
	slurm_msg_t response_msg ;
	clock_t start_time;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;
	char * node_list_ptr = NULL;
	int job_id ;
	resource_allocation_response_msg_t alloc_msg ;

	start_time = clock ();

	/* do RPC call */
	dump_job_desc(job_desc_msg);
	error_code = job_allocate(job_desc_msg, 
			&job_id, &node_list_ptr, immediate , false );

	/* return result */
	if (error_code)
	{
		info ("slurmctld_req: error %d allocating resources, time=%ld",
				error_code,  (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: allocated nodes %s, JobId=%u, time=%ld",
				node_list_ptr , job_id , 	
				(long) (clock () - start_time));
		
		/* send job_ID  and node_name_ptr */

		alloc_msg . job_id = job_id ;
		alloc_msg . node_list = node_list_ptr ;
		alloc_msg.num_cpu_groups = 0;
		response_msg . msg_type = ( immediate ) ? RESPONSE_IMMEDIATE_RESOURCE_ALLOCATION : RESPONSE_RESOURCE_ALLOCATION ;
		response_msg . data =  & alloc_msg ;

		slurm_send_node_msg ( msg->conn_fd , & response_msg ) ;
	}
	xfree ( node_list_ptr );
}

/* slurm_rpc_job_will_run - determine if job with given configuration can be initiated now */
void slurm_rpc_job_will_run ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	uint32_t job_id ;
	job_desc_msg_t * job_desc_msg = ( job_desc_msg_t * ) msg-> data ;
	char * node_name_ptr = NULL;

	start_time = clock ();

	/* do RPC call */
	dump_job_desc(job_desc_msg);
	error_code = job_allocate(job_desc_msg,
			&job_id, &node_name_ptr, false , true );
	
	/* return result */
	if (error_code)
	{
		info ("slurmctld_req: job_will_run error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: job_will_run success for , time=%ld",
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}
	xfree ( node_name_ptr ) ;

}

/* slurm_rpc_reconfigure_controller - re-initialize controller from configuration files */
void 
slurm_rpc_reconfigure_controller ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;

	start_time = clock ();

	/* do RPC call */
	error_code = init_slurm_conf ();
	if (error_code == 0)
		error_code = read_slurm_conf (SLURM_CONF);
	reset_job_bitmaps ();

	/* return result */
	if (error_code)
	{
		error ("slurmctld_req: reconfigure error %d, time=%ld",
				error_code, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: reconfigure completed successfully, time=%ld", 
				(long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

}

/* NodeConfig - determine if a node's actual configuration satisfies the
 * configured specification */
void 
slurm_rpc_node_registration ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	slurm_node_registration_status_msg_t * node_reg_stat_msg = ( slurm_node_registration_status_msg_t * ) msg-> data ;

	start_time = clock ();

	/* do RPC call */
	/*cpus = real_memory = tmp_disk = NO_VAL;
	 * this should be done client side now */
	error_code = validate_node_specs (
		node_reg_stat_msg -> node_name ,
		node_reg_stat_msg -> cpus ,
		node_reg_stat_msg -> real_memory_size ,
		node_reg_stat_msg -> temporary_disk_space ) ;

	/* return result */
	if (error_code)
	{
		error ("slurmctld_req: node_config error %d for %s, time=%ld",
				error_code, node_reg_stat_msg -> node_name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: node_config for %s, time=%ld",
				node_reg_stat_msg -> node_name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}
}

/* This may be the same as above if so remove please KBT */
void 
slurm_rpc_register_node_status ( slurm_msg_t * msg )
{
	/* init */
	int error_code;
	clock_t start_time;
	slurm_node_registration_status_msg_t * reg_msg = ( slurm_node_registration_status_msg_t * ) msg -> data ;
	start_time = clock ();

	/* do RPC call */
	error_code = 0 ;

	/* return result */
	if (error_code)
	{
		error ("slurmctld_req: register error %d on node %s, time=%ld",
				error_code, reg_msg->node_name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , error_code );
	}
	else
	{
		info ("slurmctld_req: registured node %s, time=%ld",
				reg_msg->node_name, (long) (clock () - start_time));
		slurm_send_rc_msg ( msg , SLURM_SUCCESS );
	}

}


void
init_ctld_conf ( slurm_ctl_conf_t * conf_ptr )
{
	conf_ptr->last_update       	= init_time ;
	conf_ptr->backup_interval   	= 0 ;
	conf_ptr->backup_location   	= NULL ;
	conf_ptr->backup_machine    	= NULL ;
	conf_ptr->control_daemon    	= NULL ;
	conf_ptr->control_machine   	= NULL ;
	conf_ptr->controller_timeout	= 0 ;
	conf_ptr->epilog           	= NULL ;
	conf_ptr->fast_schedule     	= 0 ;
	conf_ptr->hash_base         	= 0 ;
	conf_ptr->heartbeat_interval	= 0;
	conf_ptr->init_program      	= NULL ;
	conf_ptr->kill_wait         	= 0 ;
	conf_ptr->prioritize        	= NULL ;
	conf_ptr->prolog            	= NULL ;
	conf_ptr->server_daemon     	= NULL ;
	conf_ptr->server_timeout    	= 0 ;
	conf_ptr->slurm_conf        	= NULL ;
	conf_ptr->tmp_fs            	= NULL ;
}

void
fill_ctld_conf ( slurm_ctl_conf_t * conf_ptr )
{
	conf_ptr->last_update		= init_time ;
	conf_ptr->backup_interval   	= BACKUP_INTERVAL ;
	conf_ptr->backup_location   	= BACKUP_LOCATION ;
	conf_ptr->backup_machine   	= slurmctld_conf.backup_machine ;
	conf_ptr->control_daemon    	= CONTROL_DAEMON ;
	conf_ptr->control_machine    	= slurmctld_conf.control_machine ;
	conf_ptr->controller_timeout	= CONTROLLER_TIMEOUT ;
	conf_ptr->epilog           	= EPILOG ;
	conf_ptr->fast_schedule     	= FAST_SCHEDULE ;
	conf_ptr->hash_base         	= HASH_BASE ;
	conf_ptr->heartbeat_interval	= HEARTBEAT_INTERVAL;
	conf_ptr->init_program      	= INIT_PROGRAM ;
	conf_ptr->kill_wait         	= KILL_WAIT ;
	conf_ptr->prioritize        	= PRIORITIZE ;
	conf_ptr->prolog            	= PROLOG ;
	conf_ptr->server_daemon     	= SERVER_DAEMON ;
	conf_ptr->server_timeout   	= SERVER_TIMEOUT ;
	conf_ptr->slurm_conf       	= SLURM_CONF ;
	conf_ptr->tmp_fs            	= TMP_FS ;
}


/* Variables for commandline passing using getopt */
extern char *optarg;
extern int optind, opterr, optopt;

void 
parse_commandline( int argc, char* argv[], slurm_ctl_conf_t * conf_ptr )
{
	int c = 0;
	opterr = 0;

	while ((c = getopt (argc, argv, "b:c:f:s")) != -1)
		switch (c)
		{
			case 'b':
				conf_ptr->backup_machine = optarg;
				printf("backup_machine = %s\n", conf_ptr->backup_machine );
				break;
			case 'c':
				conf_ptr->control_machine = optarg;
				printf("control_machine = %s\n", conf_ptr->control_machine );
				break;
			case 'f':
				slurmctld_conf.slurm_conf = optarg;
				printf("slurmctrld.conf = %s\n", slurmctld_conf.slurm_conf );
				break;
			case 's':
				conf_ptr->fast_schedule = 1;
				break;
			default:
				abort ();
		}


}
