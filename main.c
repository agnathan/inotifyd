#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>

#define LOGEVENT 1
#define VERSION ("0.01 - Demo for Barracuda Networks")

// i just like the idea of "CLEAR" rather than memset to 0
#include <string.h> /* for memset, and std string funcs */
#define CLR(arg) memset(arg,0,sizeof(*arg))

#define DAEMON_NAME "[daemonname]"
#define PID_FILE "/var/run/[daemonname].pid"

/* the flags to monitor  */
#define FLAGS ( IN_ACCESS | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE | IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO | IN_OPEN)
/* size of the event structure */
#define EVENT_SIZE  (sizeof (struct inotify_event))
/** reasonable guess as to size of 1024 events */
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
/** the length of the action */
#define ACTION_LENGTH (50+FILENAME_MAX)

/** the linked list of the path we wish to listen to */
struct filedata
{
  char pathname[FILENAME_MAX];  /* the file path */
  int wd;                       /* the fd of the file */
  uint32_t listen_to;           /* the flags we should listen to with the current file */
  struct filedata *next;	/** the next record */
};

/** the record that stores information regarding inotify and the linked list */
struct file_list
{
   /** the file descriptor of inotify */
   int fd;
   /** the linked list for each requested files */
   struct filedata *files;
} ;

typedef struct filedata filestruct;
typedef struct file_list filelist;

void PrintUsage(int argc, char *argv[]) {
    if (argc >=1) {
        printf("Usage: %s -h -nn", argv[0]);
        printf("  Options:n");
        printf("      -ntDon't fork off as a daemon.n");
        printf("      -htShow this help screen.n");
        printf("n");
    }
}
 
void signal_handler(int sig) { 
    switch(sig) {
        case SIGHUP:
            syslog(LOG_WARNING, "Received SIGHUP signal.");
            break;
        case SIGTERM:
            syslog(LOG_WARNING, "Received SIGTERM signal.");
            break;
	case SIGINT:
            syslog(LOG_WARNING, "Received SIGINT, shutting down. (%d) %s", sig, strsignal(sig));
	    exit(0);
        default:
            syslog(LOG_WARNING, "Unhandled signal (%d) %s", sig, strsignal(sig));
            break;
    }
}

struct file_list * init_list(void)
{
   int fd         = inotify_init();
   if (fd < 0)
   {
     perror("inotify_init");
     return NULL;
   }

   filelist *list = (filelist *) malloc(sizeof(filelist));
   list->fd       = fd;
   list->files    = NULL;
   return list;
}

unsigned char path_exists(const char *path)
{
   struct stat buf;
   return (stat(path, &buf) == 0 || errno != ENOENT);
} 

unsigned char register_file(struct file_list *list, const char *pathname, uint32_t flags) {
  struct filedata *fdata = (struct filedata *)malloc(sizeof(struct filedata));
  
  int length          = strlen(pathname) +1;
  memset(fdata->pathname, 0, FILENAME_MAX);
  strncpy(fdata->pathname, pathname, length);
  fdata->listen_to = flags;
  fdata->next      = list->files;
  fdata->wd        = inotify_add_watch (list->fd, pathname, flags);

  if (fdata->wd < 0)
  {
    free(fdata);
    perror("inotify_add_watch");
    return 0;
  }

  list->files = fdata;
  
  return 1;
}

void populate_list(filelist *list, int argc, char *argv[])
{
   int counter;
   for (counter = 1; counter < argc; counter++)
   {
      char path[FILENAME_MAX]; 
      memset(&path, 0, FILENAME_MAX);
      strncpy(path, argv[counter], FILENAME_MAX - 1);
      if (path_exists(path))
      {
        if (! register_file(list, path, FLAGS))
        {
           perror("register_file");
        } /* if (! register_file(list, path, FLAGS)) */
      } /* if (path_exists(path)) */
      else
        fprintf(stderr,"The path \"%s\" does not exists.\n", path);
   } /* for (counter = 0; counter < argc; counter++) */
}
 
char * find_path_name(struct file_list *list, int wd)
{
  /* save starting position */
  struct filedata *filepos = list->files;
  char * name = (char *) malloc(FILENAME_MAX);
  memset(name, 0, FILENAME_MAX);

  while (NULL != filepos)
  {
     if (wd == filepos->wd)
     {
       strncpy(name, filepos->pathname, FILENAME_MAX -1);
       break;
     }
     filepos = filepos->next;
  } /* while (filepos) */

  if (strlen(name) > 0)
    return name;

  return "\0";
}

void exec_event(struct inotify_event *pevent, char *action) {
    int status;
    pid_t pid;

    pid = fork();
    if (pid == -1) { 
	/* fork error - cannot create child */
	perror("fork error - cannot create child");
	exit(1);
	}
    else if (pid == 0) {
        /* code for child */
	/* probably a few statements then an execvp() */ 
        syslog (LOG_INFO, "execing cp %s %s_copy", action, action);
        execl("/bin/cp", action, "copy", (char *)0);

	_exit(1);  /* the execvp() failed - a failed child
                      should not flush parent files */
	}
    else { /* code for parent */ 
        /* printf("Pid of latest child is %d\n", pid); */
            /* more code */
        syslog (LOG_INFO, "Healthy parent");
    }
  }

void log_event(struct inotify_event *pevent, char *action) {

    if (pevent->mask & IN_ACCESS)
      strncat(action, " was read", ACTION_LENGTH -1);
    if (pevent->mask & IN_ATTRIB)
      strncat(action, " Metadata changed", ACTION_LENGTH -1);
    if (pevent->mask & IN_CLOSE_WRITE)
      strncat(action, " was opened for writing and is now closed", ACTION_LENGTH -1);
    if (pevent->mask & IN_CLOSE_NOWRITE)
      strncat(action, " was not opened for writing and is now closed", ACTION_LENGTH -1);
    if (pevent->mask & IN_CREATE)
      strncat(action, " created in watched directory", ACTION_LENGTH -1);
    if (pevent->mask & IN_DELETE)
      strncat(action, " deleted from watched directory", ACTION_LENGTH -1);
    if (pevent->mask & IN_DELETE_SELF)
      strncat(action, "Watched file/directory was itself deleted", ACTION_LENGTH -1);
    if (pevent->mask & IN_MODIFY)
      strncat(action, " was modified", ACTION_LENGTH -1);
    if (pevent->mask & IN_MOVE_SELF)
      strncat(action, "Watched file/directory was itself moved", ACTION_LENGTH -1);
    if (pevent->mask & IN_MOVED_FROM)
      strncat(action, " moved out of watched directory", ACTION_LENGTH -1);
    if (pevent->mask & IN_MOVED_TO)
      strncat(action, " moved into watched directory", ACTION_LENGTH -1);
    if (pevent->mask & IN_OPEN)
      strncat(action, " was opened", ACTION_LENGTH -1);
    
    syslog (LOG_INFO, "Action: [%s]\n\twd=%d mask=%d cookie=%d len=%d dir=%s ", 
            action, pevent->wd, pevent->mask, pevent->cookie, pevent->len, (pevent->mask & IN_ISDIR) ? "yes":"no");

    if (pevent->len)
      syslog (LOG_INFO, "name=%s\n", pevent->name);
}

/**
 * listen to inotify events 
 *
 * @param list - A list of the files we are listen to
 *
 */
void listen(struct file_list *list)
{
  ssize_t len, i           = 0;
  char buff[EVENT_BUF_LEN] = {0};

  len = read (list->fd, buff, EVENT_BUF_LEN -1);
   
  while (i < len)
  {
    struct inotify_event *pevent    = (struct inotify_event *) & buff[i];
    char action[ACTION_LENGTH]      = {0};
    char *pathname                  = find_path_name(list, pevent->wd);

    if (pevent->len)
      strncpy (action, pevent->name, ACTION_LENGTH -1);
    else
      strncpy(action, pathname, FILENAME_MAX -1);

    free(pathname);

     /* log_event(pevent, action);  */
     exec_event(pevent, action);
   

    i += sizeof(struct inotify_event) + pevent->len;
    
  }
} 

void do_main(int argc, char *argv[]) {

   filelist *list = init_list();
   if (NULL == list)
     return;

   populate_list(list, argc, argv);

    while (1) {
      listen(list);
    }; 
}


int main(int argc, char *argv[]) {
 
#if defined(DEBUG)
    int daemonize = 0;
#else
    int daemonize = 0;
#endif
 
    // Setup signal handling before we start
    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
 
    int c;
    while( (c = getopt(argc, argv, "neh|helpi|exec")) != -1) {
        switch(c){
            case 'h':
                PrintUsage(argc, argv);
                exit(0);
                break;
            case 'n':
                daemonize = 0;
                break;
	    case 'e':
		break;
            default:
                PrintUsage(argc, argv);
                exit(0);
                break;
        }
    }
 
    syslog(LOG_INFO, "%s daemon starting up", DAEMON_NAME);
 
    // Setup syslog logging - see SETLOGMASK(3)
#if defined(DEBUG)
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog(DAEMON_NAME, LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);
#else
    setlogmask(LOG_UPTO(LOG_INFO));
    openlog(DAEMON_NAME, LOG_CONS, LOG_USER);
#endif
 
    /* Our process ID and Session ID */
    pid_t pid, sid;
 
    

    if (daemonize) {
        syslog(LOG_INFO, "starting the daemonizing process");
 
        /* Fork off the parent process */
        pid = fork();
        if (pid < 0) {
            exit(EXIT_FAILURE);
        }
        /* If we got a good PID, then
           we can exit the parent process. */
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
 
        /* Change the file mode mask */
        umask(0);
 
        /* Create a new SID for the child process */
        sid = setsid();
        if (sid < 0) {
            /* Log the failure */
            exit(EXIT_FAILURE);
        }
 
        /* Change the current working directory */
        if ((chdir("/")) < 0) {
            /* Log the failure */
            exit(EXIT_FAILURE);
        }
 
	/* this is where we would create a pid file
	   if we want this server to be a singleton */
	

        /* Close out the standard file descriptors */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
 
    //****************************************************
    // TODO: Insert core of your daemon processing here
    //****************************************************
    do_main(argc, argv);

    syslog(LOG_INFO, "%s daemon exiting", DAEMON_NAME);
 
    //****************************************************
    // TODO: Free any allocated resources before exiting
    //****************************************************
 
    exit(0);
}


 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
