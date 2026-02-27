#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
//added include headers
#include <fcntl.h>      // for open()
#include <sys/stat.h>   // for mkdir(), mkfifo()

const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  //print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}


//this func sends text to all users except the sender
void send_to_all(char *room_path, char *username, char *roomname, char *message) {
    char listfile[512];
    char cmd[1024];
    snprintf(listfile, sizeof(listfile), "/tmp/chatroom-%s/.list", roomname); //temp .list file instead of opendir/readdir because popen/pclose was blocking due to conflict with the shell's child process handling
    snprintf(cmd, sizeof(cmd), "ls /tmp/chatroom-%s > %s", roomname, listfile);//to get user list
    system(cmd);

    FILE *fp = fopen(listfile, "r"); //open in read mode
    if (!fp) return;

    char user[256];
    while (fgets(user, sizeof(user), fp)) { //loop users
        user[strcspn(user, "\n")] = 0; //rem for each filename
        if (strcmp(user, username) == 0) continue; //skip sender
        if (strcmp(user, ".list") == 0) continue;  //skip.list file

        char target_path[512];
        snprintf(target_path, sizeof(target_path), "/tmp/chatroom-%s/%s", roomname, user);//build path target

        pid_t pid = fork();
        if (pid == 0) {//child
          char formatted[1024];
          snprintf(formatted, sizeof(formatted), "[%s] %s: %s", roomname, username, message);
          int fd = open(target_path, 1);//write-only
          if (fd >= 0) {
            write(fd, formatted, strlen(formatted));
            close(fd);
          }
          exit(0);
        }
    } 
    fclose(fp);
}

//receiver continuously reading from pipe
void receiver_loop(char *user_path, char *roomname, char *username) {
    char buffer[1024];

    int fd = open(user_path, 0);  //read-only 
    if (fd < 0) return;

    while (1) {
        int n = read(fd, buffer, sizeof(buffer)-1);//reads from named pipe(FIFO)
        if (n > 0) {
          buffer[n] = '\0';
          buffer[strcspn(buffer, "\n")] = 0;
          printf("\r%s\n[%s] %s > ", buffer, roomname, username);//reprint prompt
          fflush(stdout);
        }
    }
    
    close(fd);
}

//handle user input and send message
void sender_loop(char *room_path, char *username, char *roomname) {
  char input[512];
  while (1) {
    printf("[%s] %s > ", roomname, username);//prompt
    fflush(stdout);
    if (!fgets(input, sizeof(input), stdin)) continue; //read input
    input[strcspn(input, "\n")] = 0;
    if (strcmp(input, "exit") == 0) {
        printf("Leaving chatroom...\n");
        break;
    }
    if (strlen(input) == 0) continue;
    //printf("\r[%s] %s: %s\n", roomname, username, input);
    fflush(stdout);
    //printf("[DEBUG] entering send_to_all\n"); fflush(stdout);
    send_to_all(room_path, username, roomname, input);// send message
    //printf("[DEBUG] done send_to_all\n"); fflush(stdout);
  }
  
}

//make a chatroom and handle sender/receiver processes
void run_chatroom(char *roomname, char *username){
  char room_path[512];
  sprintf(room_path, "/tmp/chatroom-%s", roomname);
  mkdir(room_path, 0777);//room dir

  char user_path[1024];
  snprintf(user_path, sizeof(user_path), "%s/%s", room_path, username);
  mkfifo(user_path, 0666);//user fifo/names pipe
  printf("Welcome to %s!\n", roomname);
  fflush(stdout);
  pid_t pid = fork();
  if (pid == 0) { //receiver child
    receiver_loop(user_path, roomname, username);
    exit(0);
  } else { // sender loop in parent
    sender_loop(room_path, username, roomname);
    kill(pid, SIGTERM);//kill receiver
    waitpid(pid, NULL, 0);
    unlink(user_path);//delete fifo
  }

}

//for REMIND command
#define MAX_REMINDERS 64
typedef struct {
    pid_t pid;
    char key[64];
    int seconds;
} ReminderEntry;

ReminderEntry active_reminders[MAX_REMINDERS];
int reminder_count = 0;
//////////////////////////////

int process_command(struct command_t *command) {
  signal(SIGCHLD, SIG_IGN);
  int r;

  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;
  
  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  //CUT
  if (strcmp(command->name, "cut") == 0) {
    /* printf(">>> MY CUT EXECUTED <<<\n");
    fprintf(stderr, "[DEBUG] arg_count=%d\n", command->arg_count);
    for (int i = 0; i < command->arg_count; i++) {
      fprintf(stderr, "[DEBUG] args[%d] = %s\n", i, command->args[i] ? command->args[i] : "NULL");
    } */
    char delimiter = '\t';//default delimiter tab
    char *fields = NULL;
    
    for (int i = 1; i < command->arg_count-1 ; i++) {
    	if (strcmp(command->args[i], "-d") == 0) {// handles d
          char *darg = command->args[i+1];
          if (darg[0] == '\'' || darg[0] == '"') delimiter = darg[1];  
          else delimiter = darg[0];
          i++;
        }
      else if (strncmp(command->args[i], "-d", 2) == 0){//handles -d compact form
        char c = command->args[i][2];
        if (c == '\'' || c == '"') c = command->args[i][3]; 
        delimiter = c;
      }
      else if (strcmp(command->args[i], "-f") == 0){//handles f
        fields = command->args[i+1];
        i++;
      }
      else if (strncmp(command->args[i], "-f", 2) == 0){//handles -f compact form
        fields = command->args[i] + 2;
      }
    }
	
    if (fields == NULL) {
      fprintf(stderr, "cut: arg missing for -f\n");
      return SUCCESS;
    }

    //parse fields
    int field_nums[100];
    int field_count = 0;

    char *fields_copy = strdup(fields);
    if (!fields_copy) { perror("strdup"); return SUCCESS; }

    char *tok = strtok(fields_copy, ",");


    while (tok != NULL) {
      field_nums[field_count++] = atoi(tok);
      tok = strtok(NULL, ",");
    }

    FILE *input = stdin;
    bool file_found = false;

    for (int i = 1; i < command->arg_count-1 ; i++) {

      if (strcmp(command->args[i], "-d") == 0 ||
        strcmp(command->args[i], "-f") == 0) {
        i++; 
        continue;
      }

      if (strncmp(command->args[i], "-d", 2) == 0 ||
        strncmp(command->args[i], "-f", 2) == 0) {
        continue; 
      }

      //if filename exists, set input to read form file
      input = fopen(command->args[i], "r");
      if (!input) {
          perror("cut");
          free(fields_copy);
          return SUCCESS;
      }

      file_found = true;
      break;
    }

    //otherwise, stdin
    char line[4096];
    while (fgets(line, sizeof(line), input)){//process each line
      line[strcspn(line, "\n")] = 0;
      char delim[2] = { delimiter, '\0' };
      char *parts[100];
      int part_count = 0;
      char *p = strtok(line, delim);
      while (p != NULL){
        parts[part_count++] = p;
        p = strtok(NULL, delim);
      }
	    for (int i = 0; i < field_count; i++){
        int idx = field_nums[i] - 1;
        if (idx < part_count && idx >= 0) printf("%s", parts[idx]);
        if (i < field_count - 1) printf("%c", delimiter);
	    }
	    printf("\n");
		}
    if (file_found) fclose(input);
    free(fields_copy);
    return SUCCESS;
  } 


   //PIPE
  if (command->next != NULL){// if pipe
    int command_index = 0;
    int num_commands = 0;
    struct command_t *tmp = command;

    //count the number of commands in pipeline
    while (tmp != NULL) {
      num_commands++;
      tmp = tmp->next;
    }
    
    pid_t pids[num_commands];//store child PID for each command in array
    struct command_t *curr = command;
    int pipes[num_commands][2];//pipe array

    int pipe_index = 0;
    for (pipe_index=0; pipe_index<num_commands-1; pipe_index++) {
        pipe(pipes[pipe_index]);// pipes[i][0] = read end, pipes[i][1] = write end
    }

    for (command_index=0; command_index<num_commands; command_index++){
      char *name = curr->name;     
      char **args = curr->args;    
      curr = curr->next; 
      
      pid_t pid = fork();
      if (pid < 0) {
          printf("ERROR: forking child process failed\n");
          exit(1);
      }
      if (pid ==0) {//child
        if ( command_index> 0) {//except first command, read input from prev commands
          dup2(pipes[command_index-1][0], STDIN_FILENO);
        }
        if (command_index < num_commands-1 ) {//except last command, replace stdout with the write end of curr pipe
          dup2(pipes[command_index][1], STDOUT_FILENO);
        }
        for (int i=0; i<num_commands-1; i++) {// close all pipes in child
          close(pipes[i][0]);
          close(pipes[i][1]);
        }
        //execute command
        if (execvp(name, args)) {
          printf("ERROR: exec child process failed\n");
          exit(1);
        }
      }
      //parent
      else{pids[command_index] = pid; }//store child PID for wait later
    }
    //close all pipe in parent
    int i = 0;
    for (i=0; i<num_commands-1; i++) {
      close(pipes[i][0]);
      close(pipes[i][1]);
    }
    //wait for children to finish
    for (int i=0; i<num_commands; i++) {
      waitpid(pids[i], NULL, WUNTRACED);
    }
    
    return SUCCESS;

  } 

  //CHATROOM
  if (strcmp(command->name, "chatroom") == 0) {
    if (command->arg_count < 3) {
      printf("Usage Format: chatroom <roomname> <username>, enter roomane + username\n");
      return SUCCESS;
    }
    /* pid_t pid = fork();
    if (pid == 0) { */
      run_chatroom(command->args[1], command->args[2]);
      /* exit(0);
    }
    if (!command->background) waitpid(pid, NULL, 0); */
    return SUCCESS;
  }


//REMIND COMMAND
if (strcmp(command->name, "remind") == 0) {

  // for cancelling
  if (command->arg_count >= 3 && strcmp(command->args[1], "cancel") == 0){
    char *key = command->args[2];
    int found = 0;
    for (int i = 0; i < reminder_count; i++) {
      if (strcmp(active_reminders[i].key, key) == 0){
        //
        char fifo_path[256];
        snprintf(fifo_path, sizeof(fifo_path), "/tmp/remind_%d", active_reminders[i].pid);
        int fd = open(fifo_path, 1);
        if (fd >= 0) {
          write(fd, "cancel", 6);
          close(fd);
        }
        //remove from active reminders list by replacinf with last reminder
        active_reminders[i] = active_reminders[--reminder_count];
        found = 1;
        printf("reminder '%s' is cancelled.\n", key);
        break;
      }
    }
    if (!found) printf("no reminder with key '%s'.\n", key);
    return SUCCESS;
  }

  //validate args
  if (command->arg_count < 5) {
    printf("Usage: remind <key> <seconds> <message>\n");
    return SUCCESS;
  }
  char *key= command->args[1];
  int  seconds  = atoi(command->args[2]);
  if (seconds <= 0) {
    printf("Time is not negative, yet. Enter a positve number of seconds.\n");
    return SUCCESS;
  }

  //reclaim the slot if key exists but process is finished, to reuse key
  for (int i = 0; i < reminder_count; i++){
    if (strcmp(active_reminders[i].key, key) == 0){
      if (kill(active_reminders[i].pid, 0) != 0) {
        //process is gone, reclaim slot
        active_reminders[i] = active_reminders[--reminder_count];
        break;
      }
      printf("A reminder with key '%s' already exists :((\n", key);
      return SUCCESS;
    }
  }
  if (reminder_count >= MAX_REMINDERS) {
    printf("I can't remember this much stuff :((\n");
    return SUCCESS;
  }

  //build full message
  char full_message[1024] = "";
  for (int i = 3; i < command->arg_count; i++) {
    if (command->args[i] == NULL) break;
    strncat(full_message, command->args[i], sizeof(full_message) - strlen(full_message) - 1);
    strncat(full_message, " ", sizeof(full_message) - strlen(full_message) - 1);
  }

  //fork child to wait in background
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    exit(0);
  }

  if (pid == 0) {//child, runs in background
    // Unique FIFO per reminder by its PID
    char fifo_path[256];
    snprintf(fifo_path, sizeof(fifo_path),"/tmp/remind_%d", getpid());
    if (mkfifo(fifo_path, 0666) == -1) {
      perror("mkfifo");
      exit(1);
    }

    // opening fifo in NONBLOCK mode so reads dont stall
    int fifo_fd = open(fifo_path, 0 | 2048);

    if (fifo_fd < 0) {
      perror("open fifo");
      unlink(fifo_path);
      exit(1);
    }

    //count down, continuously checking for cancel signal
    for (int elapsed = 0; elapsed < seconds; elapsed++) {
      sleep(1);

      // Check for cancel signal
      char buf[16];
      int n = read(fifo_fd, buf, sizeof(buf));
      if (n > 0) {
        close(fifo_fd);
        unlink(fifo_path);
        exit(0);
      }
    }
    //timer expired, remind the forgetful user
    printf("\n\n!!! REMINDER !!! -> %s\n", full_message);
    show_prompt();

    close(fifo_fd);
    unlink(fifo_path);
    exit(0);
  }
  //parent process
  signal(SIGCHLD, SIG_IGN); //to avoid zombies
  strncpy(active_reminders[reminder_count].key, key, sizeof(active_reminders[0].key) - 1);
  active_reminders[reminder_count].pid     = pid;
  active_reminders[reminder_count].seconds = seconds;
  reminder_count++;

  printf("Reminder set for %d seconds.\n", seconds);
  return SUCCESS;
  }

 
  //signal(SIGCHLD, SIG_IGN);
  pid_t pid = fork();
  if (pid == 0) // child
  {
    FILE *fp;

    if (command->redirects[0]){ // input redirection <
    	fp = fopen(command->redirects[0],"r"); //open file in r mode
      if (!fp) {perror("input"); exit(1);}
      dup2(fileno(fp), STDIN_FILENO);//read from the file 
      fclose(fp);//close file
    }

    if (command->redirects[1]){//output redirection >
    	fp = fopen(command->redirects[1],"w");//open in write mode
      if (!fp){perror("output"); exit(1);}
      dup2(fileno(fp), STDOUT_FILENO);// write
      fclose(fp);
    }

    if (command->redirects[2]){// append redirection
    	fp = fopen(command->redirects[2], "a");//open in append mode
      if (!fp){perror("append"); exit(1);}
      dup2(fileno(fp), STDOUT_FILENO);//append
      fclose(fp);
    }

    if (strchr(command->name, '/')!=NULL){
      execv(command->name, command->args);
      perror("execv failed");
      exit(1);
    }
    else{
      char *path = getenv("PATH"); // get the path from environ
      char *path_copy = strdup(path); // copy path
      char *dir = strtok(path_copy, ":");
      while (dir !=NULL){ // try evey directory in path
        char fpath[1024];
        sprintf(fpath, "%s/%s", dir,command->name);
        execv(fpath, command->args);
        dir = strtok(NULL, ":");
      }
      printf("-%s: %s: command not found\n", sysname, command->name);
      free(path_copy);
      exit(127);
    }
     
  } else { // parent
    if (!command->background){ 
	    waitpid(pid, NULL, 0);  // if background==false, run in foreground, then wait for this child to finish
    }
    return SUCCESS;
  }
}


int main() {
  while (1) {
    struct command_t *command = (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
