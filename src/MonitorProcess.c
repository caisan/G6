#include "G6.h"

signed char		g_exit_flag = 0 ;
signed char		g_SIGUSR1_flag = 0 ;
signed char		g_SIGUSR2_flag = 0 ;
signed char		g_SIGTERM_flag = 0 ;

static void sig_set_flag( int sig_no )
{
	UPDATEDATETIMECACHE
	InfoLog( __FILE__ , __LINE__ , "recv signal[%d]" , sig_no );
	
	if( sig_no == SIGUSR1 )
	{
		g_SIGUSR1_flag = 1 ;
	}
	else if( sig_no == SIGUSR2 )
	{
		g_SIGUSR2_flag = 1 ;
	}
	else if( sig_no == SIGTERM )
	{
		g_SIGTERM_flag = 1 ;
	}
	
	return;
}

static void sig_proc( struct ServerEnv *penv )
{
	int		nret = 0 ;
	
	if( g_SIGUSR1_flag == 1 )
	{
		/* 通知所有线程关闭日志文件描述字，后续写日志时会再次自动打开 */
		InfoLog( __FILE__ , __LINE__ , "write accept_command_pipe[%d] L ..." , g_penv->accept_command_pipe.fds[1] );
		nret = write( g_penv->accept_command_pipe.fds[1] , "L" , 1 ) ;
		InfoLog( __FILE__ , __LINE__ , "write accept_command_pipe[%d] L done[%d]" , g_penv->accept_command_pipe.fds[1] , nret );
		
		g_SIGUSR1_flag = 0 ;
	}
	else if( g_SIGUSR2_flag == 1 )
	{
		struct ForwardSession	*forward_session_array = NULL ;
		
		pid_t			pid ;
		
		int			nret = 0 ;
		
		/* 测试剩余内存 */
		forward_session_array = (struct ForwardSession *)malloc( sizeof(struct ForwardSession) * penv->cmd_para.forward_session_size ) ;
		if( forward_session_array == NULL )
		{
			ErrorLog( __FILE__ , __LINE__ , "malloc failed , momery not enough , errno[%d]" , errno );
			return;
		}
		free( forward_session_array );
		
		/* 保存侦听socket到环境变量里 */
		nret = SaveListenSockets( g_penv ) ;
		if( nret )
		{
			ErrorLog( __FILE__ , __LINE__ , "SaveListenSockets faild[%d]" , nret );
			return;
		}
		
		/* 通知所有线程关闭侦听socket */
		InfoLog( __FILE__ , __LINE__ , "write accept_command_pipe[%d] Q ..." , g_penv->accept_command_pipe.fds[1] );
		nret = write( g_penv->accept_command_pipe.fds[1] , "Q" , 1 ) ;
		InfoLog( __FILE__ , __LINE__ , "write accept_command_pipe[%d] Q done[%d]" , g_penv->accept_command_pipe.fds[1] , nret );
		
		/* 创建子进程，用新程序映像覆盖代码段 */
		pid = fork() ;
		if( pid == -1 )
		{
			;
		}
		else if( pid == 0 )
		{
			execvp( "G6" , g_penv->argv );
			
			exit(9);
		}
		
		/* 设置退出标志 */
		g_SIGUSR2_flag = 0 ;
		g_exit_flag = 1 ;
		DebugLog( __FILE__ , __LINE__ , "set g_exit_flag[%d]" , g_exit_flag );
	}
	else if( g_SIGTERM_flag == 1 )
	{
		int		nret = 0 ;
		
		/* 通知所有线程关闭侦听socket */
		InfoLog( __FILE__ , __LINE__ , "write accept_command_pipe[%d] Q ..." , g_penv->accept_command_pipe.fds[1] );
		nret = write( g_penv->accept_command_pipe.fds[1] , "Q" , 1 ) ;
		InfoLog( __FILE__ , __LINE__ , "write accept_command_pipe[%d] Q done[%d]" , g_penv->accept_command_pipe.fds[1] , nret );
		
		/* 设置退出标志 */
		g_SIGTERM_flag = 0 ;
		g_exit_flag = 1 ;
		DebugLog( __FILE__ , __LINE__ , "set g_exit_flag[%d]" , g_exit_flag );
	}
	
	return;
}

int MonitorProcess( struct ServerEnv *penv )
{
	struct sigaction	act ;
	
	pid_t			pid ;
	int			status ;
	
	int			nret = 0 ;
	
	/* 设置日志输出文件 */
	SETPID
	SETTID
	UPDATEDATETIMECACHE
	InfoLog( __FILE__ , __LINE__ , "--- G6.MonitorProcess ---" );
	
	/* 设置信号句柄 */
	act.sa_handler = & sig_set_flag ;
	sigemptyset( & (act.sa_mask) );
	act.sa_flags = 0 ;
	
	signal( SIGCLD , SIG_DFL );
	signal( SIGCHLD , SIG_DFL );
	signal( SIGPIPE , SIG_IGN );
	sigaction( SIGTERM , & act , NULL );
	sigaction( SIGUSR1 , & act , NULL );
	sigaction( SIGUSR2 , & act , NULL );
	
	/* 主工作循环 */
	while( g_exit_flag == 0 )
	{
		/* 创建子进程 */
		_FORK :
		penv->pid = fork() ;
		if( penv->pid == -1 )
		{
			if( errno == EINTR )
			{
				sig_proc( penv );
				goto _FORK;
			}
			
			ErrorLog( __FILE__ , __LINE__ , "fork failed , errno[%d]" , errno );
			return -1;
		}
		else if( penv->pid == 0 )
		{
			SETPID 
			SETTID
			UPDATEDATETIMECACHE
			
			InfoLog( __FILE__ , __LINE__ , "child : [%ld]fork[%ld]" , getppid() , getpid() );
			
			nret = WorkerProcess( penv ) ;
			
			CleanEnvirment( penv );
			
			exit(-nret);
		}
		else
		{
			InfoLog( __FILE__ , __LINE__ , "parent : [%ld]fork[%ld]" , getpid() , penv->pid );
		}
		
		if( penv->cmd_para.close_log_flag == 1 )
			CloseLogFile();
		
		/* 监控子进程结束 */
		_WAITPID :
		pid = waitpid( -1 , & status , 0 );
		UPDATEDATETIMECACHE
		if( pid == -1 )
		{
			if( errno == EINTR )
			{
				sig_proc( penv );
				goto _WAITPID;
			}
			
			ErrorLog( __FILE__ , __LINE__ , "waitpid failed , errno[%d]" , errno );
			close( penv->accept_command_pipe.fds[1] );
			return -1;
		}
		if( pid != penv->pid )
			goto _WAITPID;
		
		ErrorLog( __FILE__ , __LINE__
			, "waitpid[%ld] WEXITSTATUS[%d] WIFSIGNALED[%d] WTERMSIG[%d]"
			, penv->pid , WEXITSTATUS(status) , WIFSIGNALED(status) , WTERMSIG(status) );
		
		if( g_exit_flag == 0 )
			sleep(1);
	}
	
	InfoLog( __FILE__ , __LINE__ , "exit" );
	
	return 0;
}

int _MonitorProcess( void *pv )
{
	return MonitorProcess( (struct ServerEnv *)pv );
}

