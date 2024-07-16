/* CSCI-4061 Fall 2022
* Group Member #1: Michael Vang vang2891
* Group Member #2: Vaibhav Jain jain0232
* Group Member #3: Matin Horri horri031
*/

#include "wrapper.h"
#include "util.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <signal.h>

#define MAX_TABS 100  // this gives us 99 tabs, 0 is reserved for the controller
#define MAX_BAD 1000
#define MAX_URL 100
#define MAX_FAV 100
#define MAX_LABELS 100


comm_channel comm[MAX_TABS];         // Communication pipes
char favorites[MAX_FAV][MAX_URL];    // Maximum char length of a url allowed
int num_fav = 0;                     // # favorites

typedef struct tab_list {
 int free;
 int pid; // may or may not be useful
} tab_list;

// Tab bookkeeping
tab_list TABS[MAX_TABS];


// return total number of tabs that are being used
int get_num_tabs () {
 int counter = 0;
 for(int i = 1; i < MAX_TABS; i++) {
   if(TABS[i].free == 0) {
     counter++;
   }
 }
 return counter;
}

// get next free tab index
int get_free_tab () {
 for(int i = 1; i < MAX_TABS; i++) {
   if(TABS[i].free == 1) {
     return i;
   }
 }
 return -1;
}

// init TABS data structure
void init_tabs () {
 int i;

 for (i=1; i<MAX_TABS; i++)
   TABS[i].free = 1;
 TABS[0].free = 0;
}

/***********************************/
/* Favorite manipulation functions */
/***********************************/

// return 0 if favorite is ok, -1 otherwise
// both max limit, already a favorite (Hint: see util.h) return -1
int fav_ok (char *uri) {

 if(num_fav >= MAX_FAV){
   return -1;
 }
 if(on_favorites(uri)){  //checks if url is already in the fav list
   return -1;
 }
 return 0;
}

// Add uri to favorites file and update favorites array with the new favorite
void update_favorites_file (char *uri) {

 FILE *ptr = fopen(".favorites", "a");    //open the passed file as an argument, and write to the file in append mode

 if (ptr != NULL){
     strcpy(favorites[num_fav], uri);
     num_fav++;
     fprintf(ptr, "\n%s", uri);
 }
 fclose(ptr);
 return;
}

// Set up favorites array using the favorites file provided
void init_favorites (char *fname) {

 FILE *ptr = fopen(fname, "r");    //open the passed file as an argument, and read the file.

 if(ptr!=NULL) {

   char buffer[MAX_URL];

   while(fscanf(ptr, "%s", buffer) == 1) { //reads formatted input from the file
     strcpy(favorites[num_fav], buffer);
     num_fav++;
   }
 }
 fclose(ptr);
}

// Make fd non-blocking just as in class!
// Return 0 if ok, -1 otherwise
// Really a util but I want you to do it :-)
int non_block_pipe (int fd) {

 int nFlags;

 if ((nFlags = fcntl(fd, F_GETFL, 0)) < 0)
   return -1;
 if(fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
   return -1;
 }
 return 0;
}

/***********************************/
/* Functions involving commands    */
/***********************************/

// Checks if tab is bad and url violates constraints; if so, return.
// Otherwise, send NEW_URI_ENTERED command to the tab on inbound pipe
void handle_uri (char *uri, int tab_index) {

 if(bad_format(uri)) {
   alert("BAD FORMAT");
   return;
 }

 if(tab_index==0 || TABS[tab_index].free==1) {  //if tab is free (isn't active) or not tabs exist
   alert("BAD TAB");
   return;
 }

 if(on_blacklist(uri)) {
   alert("BLACKLIST");
   return;
 }

 if(strlen(uri) >= MAX_URL){
   alert("BAD URL");            //url Length exceeds 100
   return;
 }

 //Creating the command x to be sent
 req_t x = {
   .type = NEW_URI_ENTERED,
   .tab_index = tab_index};
 strcpy(x.uri, uri);
 printf("URL selected is %s\n", x.uri);

 write(comm[tab_index].inbound[1], &x, sizeof(req_t));
}


// A URI has been typed in, and the associated tab index is determined
// If everything checks out, a NEW_URI_ENTERED command is sent
void uri_entered_cb (GtkWidget* entry, gpointer data) {

 if(data == NULL) {
   return;
 }

 int tab = query_tab_id_for_request(entry, data);  // Get the tab
 char* url = get_entered_uri(entry);               // Get the URL
 handle_uri(url, tab);                             // Does error checking on tabs and URL
}

// Called when + tab is hit
// Check tab limit ... if ok then do some heavy lifting (see comments)
// Create new tab process with pipes
void new_tab_created_cb (GtkButton *button, gpointer data) {

 if (data == NULL) {
   return;
 }

 if (get_num_tabs() == MAX_TABS) {         // at tab limit, no new tabs will be created
   return;
 }

 int free_tab = get_free_tab();            // Get a free tab

 // Create communication pipes for this tab
 if((pipe(comm[free_tab].inbound) < 0) || (pipe(comm[free_tab].outbound) < 0)) {
   perror("Pipe failed for free_tab\n");
   return;
 }
 // Make the read ends non-blocking
 if(non_block_pipe(comm[free_tab].inbound[0]) == -1 || non_block_pipe(comm[free_tab].outbound[0]) == -1) {
   perror("Pipe nonblock unsucessful for free_tab");
   return;
 }

 // fork and create new render tab
 pid_t tabRenderPID = fork();
 if(tabRenderPID == 0) {                        // CHILD
   char tab_index[5];                          //String for tab_index
   sprintf(tab_index, "%d", free_tab);

   char pipe_str[20];
   sprintf (pipe_str, "%d %d %d %d", comm[free_tab].inbound[0], comm[free_tab].inbound[1], comm[free_tab].outbound[0], comm[free_tab].outbound[1]);

   execl("./render", "render", tab_index, pipe_str, NULL);
   perror("Child failed to exec");
   exit(-1);
 }
 else {
   TABS[free_tab].free = 0;                    //Set tab to be used or non-free
   TABS[free_tab].pid = getpid();
 }
}

// This is called when a favorite is selected for rendering in a tab
// Adding "https://" first to the uri so it can be rendered as favorites strip this off for a nicer looking menu
void menu_item_selected_cb (GtkWidget *menu_item, gpointer data) {

 if (data == NULL) {
   return;
 }

 // Note: For simplicity, currently we assume that the label of the menu_item is a valid url
 char *basic_uri = (char *)gtk_menu_item_get_label(GTK_MENU_ITEM(menu_item));
 char uri[MAX_URL];                    //append "https://" for rendering

 sprintf(uri, "https://%s", basic_uri);

 int tab_index = query_tab_id_for_request(menu_item, data);   // Get the tab
 handle_uri (uri, tab_index);
 return;
}


//The controller now runs an loop so it can check all pipes for the differnt types of commands it might receive and take the required action
int run_control() {
 browser_window * b_window = NULL; 		//initialize a pointer to the browser_window structure(wrapper.h)
 int i, nRead;					//nread: Stores the return value of the read function which is for pipes
 req_t req;					//This represents a command/message sent in a pipe
 req_type command;				//type of commands/messages

 //Create controller window
 create_browser(CONTROLLER_TAB, 0, G_CALLBACK(new_tab_created_cb),
    G_CALLBACK(uri_entered_cb), &b_window, comm[0]);

 // Create favorites menu
 create_browser_menu(&b_window, &favorites, num_fav);

 while (1) {
   process_single_gtk_event();

   // Read from all tab pipes including private pipe (index 0) to handle commands
   // Loop across all pipes from VALID tabs -- starting from 0d
   for (i=0; i<MAX_TABS; i++) {
     if(TABS[i].free){//if Tab is free, skip
       continue;
     }
     nRead = read(comm[i].outbound[0], &req, sizeof(req_t));   //write from the tab and read from the controller

     // Check that nRead returned something before handling cases
     if(nRead == -1) {
       continue;
     }

     //Case 1: PLEASE_DIE (controller should die, self-sent), and send PLEASE_DIE to all tabs
     if(req.type == PLEASE_DIE) {
       command = PLEASE_DIE;
       for(int i = 1; i < MAX_TABS; i++) {
         if(TABS[i].free){ //Don't want to send PLEASE_DIE to free tabs.
           continue;
         }
         write(comm[i].inbound[1], &command, sizeof(req_type));
       }
       exit(0);
     }

     // Case 2: TAB_IS_DEAD (tab has exited after clicking on X, wait to free up resources, and set free to 1)
     if(req.type == TAB_IS_DEAD) {
       wait(NULL);
       TABS[req.tab_index].free = 1;
     }

     // Case 3: IS_FAV (add uri to favorite menu, and update .favorites)
     if(req.type == IS_FAV) {
       if(fav_ok(req.uri) == 0) {		//check if we already don't have it in the favorite list(Avoiding duplicate)
         update_favorites_file(req.uri);		// Add uri to favorites file and update favorites array with the new favorite
         add_uri_to_favorite_menu(b_window, req.uri);		//add the uri to the list
       }
       else {
         alert("FAV EXISTS");
       }
     }
   }
   usleep(1000);
 }
 return 0;
}

//handles the creation of the controller process and initalizing the favorites and blacklist
int main(int argc, char **argv)
{
 if (argc != 1) {
   fprintf (stderr, "browser <no_args>\n");
   exit (0);
 }

 init_tabs ();
 init_blacklist(".blacklist");
 init_favorites(".favorites");

 pid_t pid = fork();               //Fork controller
 TABS[0].pid = getppid();

 //Child Process
 if(pid == 0) {
   //Error checking and giving controller its private pipes
   if((pipe(comm[0].inbound) < 0) || (pipe(comm[0].outbound) < 0)) {
     perror("Pipe failed\n");
     exit(1);
   }

   //Error checking and setting read ends of channel to nonblock
   if(non_block_pipe(comm[0].inbound[0]) == -1 || non_block_pipe(comm[0].outbound[0]) == -1) {
     perror("Pipe nonblock unsucessful");
     exit(1);
   }
   run_control();
 }
 else{
   //wait for the Controller process to reclaim.
   wait(NULL);
   exit(0);
 }
}
