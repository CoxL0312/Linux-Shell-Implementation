/**
 * @file myshell.c
 * @author ** Lindsey Cox **
 * @date ** August 17, 2025 **
 * @brief Acts as a simple command line interpreter.  It reads commands from
 *        standard input entered from the terminal and executes them. The
 *        shell does not include any provisions for control structures,
 *        redirection, background processes, environmental variables, pipes,
 *        or other advanced properties of a modern shell. All commands are
 *        implemented internally and do not rely on external system programs.
 *
 */

#include <pwd.h>
#include <time.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>

#define BUFFER_SIZE          256
#define MAX_PATH_LENGTH      256
#define MAX_FILENAME_LENGTH  256

static char buffer[BUFFER_SIZE] = {0};
static char filename[MAX_FILENAME_LENGTH] = {0};

// Implements various UNIX commands using POSIX system calls
// Each must return an integer value indicating success or failure
int do_cat(const char* filename);
int do_cd(char* dirname);
int do_ls(const char* dirname);
int do_mkdir(const char* dirname);
int do_pwd(void);
int do_rm(const char* filename);
int do_rmdir(const char* dirname);
int do_stat(char* filename);
int execute_command(char* buffer);
DIR* d;
  
/*
* @brief  used in the ls function for identifying if an entry is a 
*         file or directory
*@return "DIR" if a directory and "FILE" otherwise
*/
static const char* file_or_dir(mode_t mode)
{
  return S_ISDIR(mode) ? "DIR" : "FILE";
}

/*
*@brief file type helper for the ls function
*@return a string noting the file type
*
*/
static const char* ftype_string(mode_t mode) 
{
  switch (mode & S_IFMT)
  {
    case S_IFREG: return "regular";
    case S_IFDIR: return "directory";
    case S_IFLNK: return "symlink";
    case S_IFCHR: return "char-device";
    case S_IFBLK: return "block-device";
    case S_IFIFO: return "fifo";
    case S_IFSOCK: return "socket";
    default:      return "unknown";
  }
}



/**
 * @brief  Removes extraneous whitespace at the end of a command to avoid
 *         parsing problems
 * @param  Char array representing the command entered
 * @return None
 */
void strip_trailing_whitespace(char* string) {
  int i = strnlen(string, BUFFER_SIZE) - 1;
  
  while(isspace(string[i]))
    string[i--] = 0;
}

/**
 * @brief Displays a command prompt including the current working directory
 */
void display_prompt(void) {
  char current_dir[MAX_PATH_LENGTH];
  
  if (getcwd(current_dir, sizeof(current_dir)) != NULL)
    // Outputs the current working directory in bold green text (\033[32;1m)
    // \033 is the escape sequence for changing text, 32 is green, 1 is bold
    fprintf(stdout, "myshell:\033[32;1m%s\033[0m> ", current_dir);
}

/**
 * @brief  Main program function
 * @param  Not used
 * @return EXIT_SUCCESS is always returned
 */
int main(int argc, char** argv) {
  while (true) {
    display_prompt();
    
    // Read a line representing a command to execute from stdin into 
    // a character array
    if (fgets(buffer, BUFFER_SIZE, stdin) != 0) {
      
      // Clean up sloppy user input
      strip_trailing_whitespace(buffer);
      
      //Reset filename buffer after each command execution
      bzero(filename, MAX_FILENAME_LENGTH);     
      
      // As in most shells, "cd" and "exit" are special cases that need
      // to be handled separately
      if ((sscanf(buffer, "cd %s", filename) == 1) ||
	  (!strncmp(buffer, "cd", BUFFER_SIZE))) 
	do_cd(filename);
      else if (!strncmp(buffer, "exit", BUFFER_SIZE)) 
	exit(EXIT_SUCCESS);
      else 
	execute_command(buffer);
    }
  }
  
  return EXIT_SUCCESS;
}

/**
 * @brief  Changes the current working directory
 * @param  Char array representing the directory to change to, or if empty
 *         use the user's home directory
 * @return -1 on error, 0 on success
 */
int do_cd(char* dirname) {
  struct passwd *p = getpwuid(getuid());

  // If no argument, change to current user's home directory
  if (strnlen(dirname, MAX_PATH_LENGTH) == 0)
      strncpy(dirname, p->pw_dir, MAX_PATH_LENGTH);

  // Otherwise, change to directory specified and check for error
  if (chdir(dirname) < 0) {
    fprintf(stderr, "cd: %s\n", strerror(errno));
    return -1;
  }
  
  return 0;
}

/**
 * @brief  Lists the contents of a directory
 * @param  Name of directory to list, or if empty, use current working directory
 * @return -1 on error, 0 on success
 * Notes: lstat > stat because it's apparently better for identifying file types, aka lsat is more portable and reliable
 * I added functionality to list whether each entry is a file or a directory, as well as what type of file it would be if not a directory 
 * (note ftype_string) above, and then list the num of bytes each entry takes up
 */ 
int do_ls(const char* dirname) {
  //normalize input
  //if dirname is null or empty string, use ">""
  const char* dir = (dirname && dirname[0]) ? dirname : ".";

  DIR* d = opendir(dir);
  if (!d)
  {
    fprintf(stderr, "Could not open directory %s %s\n", dirname, strerror(errno));
    return -1;
  }

  struct dirent* entry;
  errno = 0; //do this for error handling later to distinguish end of stream from error

  while ((entry = readdir(d)) != NULL)
  {
    //build a full path to the entry for lstat call
    //snprintf into a fixed buffer, then check for truncation with n>= buffer or any format errors
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
    if (n < 0 || (size_t)n >= sizeof(path)) 
    {
      fprintf(stderr, "path too long: %s/%s\n", dir, entry->d_name); //if it doesn't fit, note it but move on
      continue;
    }

    struct stat st;
    if (lstat(path, &st) != 0) //get the data if symlink
    {
      fprintf(stderr, "lstat failed for '%s': %s\n", path, strerror(errno));
      continue;
    }

    //get dir/file label and file type
    const char* fod = file_or_dir(st.st_mode);
    const char* ftype = ftype_string(st.st_mode);
    off_t size = st.st_size; //bytes of the file

    //omg this one line took eighteen years
    printf("%s\t[%s]\t(type=%s)\tsize=%jd bytes \n", entry->d_name, fod, ftype, (intmax_t)size);
  }

  //post loop error check
  if (errno != 0) //error not just end of stream here!
  {
    fprintf(stderr, "Error reading directory %s :  %s\n", dir, strerror(errno));
    closedir(d);
    return -1;
  }

  if (closedir(d) != 0)
  {
    fprintf(stderr, "error closing directory %s :  %s\n", dir, strerror(errno));
    return -1;
  }
  return 0;
}

/**
 * @brief  Outputs the contents of a single ordinary file
 * @param  Name of file whose contents should be output
 * @return -1 on error, 0 on success
 */
int do_cat(const char* filename) {
  //take names of files as input and print entire contents of each, in sequential order
  int sourcefd = open(filename, O_RDONLY, 0);
  if (sourcefd < 0) //source file not opened, failure
  {
    fprintf(stderr, "Unable to open %s: %s\n", filename, strerror(errno));
    close(sourcefd);
    return -1;
  }

  int countr = 1;
  int exitLoop = 1;
  while (exitLoop == 1)
  {
    countr = read(sourcefd, buffer, BUFFER_SIZE);
    if (countr < 0) //error reading data
    {
        fprintf(stderr, "Error reading data from %s: %s\n", filename, strerror(errno));
        return -1;
    }
    else if (countr == 0) //file empty now
    {
      fprintf(stdout, "\n");
      exitLoop = 0;
    }
    else
    {
      fwrite(buffer, BUFFER_SIZE, BUFFER_SIZE -1, stdout);
      fflush(stdout);
      //printf("%s\n", buffer);
    }
  }

  //close the file
  int sourceCloseSucc = close(sourcefd);
  if (sourceCloseSucc < 0) 
  {
      fprintf(stderr, "Error closing source file %s: %s\n", filename, strerror(errno));
      exit-1;
  }

  close(sourcefd);
}

/**
 * @brief  Creates a new directory
 * @param  Name of directory to create
 * @return -1 on error, 0 on success
 */
int do_mkdir(const char* dirname) {

  //creates a new directory named dirname with read, write, and search permissions for owner and group
  int status = mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
  if (status == -1)
  {
    fprintf(stderr, "Error creating directory %s %s\n", dirname, strerror(errno));
    return -1;
  }
  return 0;
}

/**
 * @brief  Removes an existing directory
 * @param  Name of directory to remove
 * @return -1 on error, 0 on success
 */
int do_rmdir(const char* dirname) {
  int status = rmdir(dirname);
  if (status == -1)
  {
    fprintf(stderr, "Error removing directory %s %s\n", dirname, strerror(errno));
    return -1;
  }
  return 0;
}

/**
 * @brief  Outputs the name of the current working directory
 * @return Cannot fail. Always returns 1.
 */
int do_pwd(void) {
char current_dir[MAX_PATH_LENGTH];
if (getcwd(current_dir, sizeof(current_dir)) != NULL)
{
  fprintf(stdout, "myshell:\033[32;1m%s\033[0m> ", current_dir);
}
else
{
  fprintf(stderr, "Error outputting current directory: %s\n", strerror(errno));
  return -1;
}
fprintf(stdout, "\n");
return 0;
}


/**
 * @brief  Removes (unlinks) a file
 * @param  Name of file to delete
 * @return -1 on error, 0 on success
 */
int do_rm(const char* filename) {
  int status = unlink(filename);
  if (status == -1)
  {
    fprintf(stderr, "Error removing file %s %s\n", filename, strerror(errno));
    return -1;
  }
  return 0;
}

/**
 * @brief  Outputs information about a file
 * @param  Name of file to stat
 * @return -1 on error, 0 on success
 */
int do_stat(char* filename) {
  struct stat sb;
  stat(filename, &sb);

  if (stat(filename, &sb) == -1)
  {
    fprintf(stdout, "Error outputting stat: %s, %s\n", filename, strerror(errno));
    return -1;
  }
  else
  {
    fprintf(stdout, "File: %s\n", filename);
    fprintf(stdout, "Size: %ld bytes\t", (long) sb.st_blksize);
    fprintf(stdout, "Blocks: %ld\t", (long) sb.st_blocks);
    fprintf(stdout, "Links: %ld\n", (long) sb.st_nlink);

    fprintf(stdout, "Inode: %ld\n", (long) sb.st_ino);
    fprintf(stdout, "Time Modified: %s\n", ctime(&sb.st_mtime));
  }

}

/**
 * @brief  Executes a shell command. Checks for invalid commands.
 * @param  Char array representing the command to execute
 * @return Return value of command being executed, or -1 for invalid command
 */
int execute_command(char* buffer)  {
  if (sscanf(buffer, "cat %s", filename) == 1) {
    return do_cat(filename);
  }
  
  if (sscanf(buffer, "stat %s", filename) == 1) {
    return do_stat(filename);
  }
  
  if (sscanf(buffer, "mkdir %s", filename) == 1) {
    return do_mkdir(filename);
  }
  
  if (sscanf(buffer, "rmdir %s", filename) == 1) {
    return do_rmdir(filename);
  }
  
  if (sscanf(buffer, "rm %s", filename) == 1) {
    return do_rm(filename);
  }
 
  if ((sscanf(buffer, "ls %s", filename) == 1) ||
      (!strncmp(buffer, "ls", BUFFER_SIZE))) {
    if (strnlen(filename, BUFFER_SIZE) == 0)
      sprintf(filename, ".");

    return do_ls(filename);
  }

  if (!strncmp(buffer, "pwd", BUFFER_SIZE)) {
    return do_pwd();
  }

  // Invalid command
  if (strnlen(buffer, BUFFER_SIZE) != 0)
    fprintf(stderr, "myshell: %s: No such file or directory\n", buffer);

  return -1;
}
