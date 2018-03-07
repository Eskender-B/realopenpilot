// termux-api.c - helper binary for calling termux api classes
// Usage: termux-api ${API_METHOD} ${ADDITIONAL_FLAGS}
//        This executes
//          am broadcast com.termux.api/.TermuxApiReceiver --es socket_input ${INPUT_SOCKET} 
//                                                        --es socket_output ${OUTPUT_SOCKET}
//                                                        --es ${API_METHOD}
//                                                        ${ADDITIONAL_FLAGS}
//        where ${INPUT_SOCKET} and ${OUTPUT_SOCKET} are addresses to linux abstract namespace sockets,
//        used to pass on stdin to the java implementation and pass back output from java to stdout.
#define _POSIX_SOURCE
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <zmq.h>

#define S_NOTIFY_MSG " "
#define S_ERROR_MSG "Error while writing to self-pipe.\n"
static int s_fd;
static void s_signal_handler (int signal_value)
{
    int rc = write (s_fd, S_NOTIFY_MSG, sizeof(S_NOTIFY_MSG));
    if (rc != sizeof(S_NOTIFY_MSG)) {
        write (STDOUT_FILENO, S_ERROR_MSG, sizeof(S_ERROR_MSG)-1);
        exit(1);
    }
}

static void s_catch_signals (int fd)
{
    s_fd = fd;

    struct sigaction action;
    action.sa_handler = s_signal_handler;
    //  Doesn't matter if SA_RESTART set because self-pipe will wake up zmq_poll
    //  But setting to 0 will allow zmq_read to be interrupted.
    action.sa_flags = 0;
    sigemptyset (&action.sa_mask);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGTERM, &action, NULL);
}
// Function which execs "am broadcast ..".
_Noreturn void exec_am_broadcast(int argc, char** argv, char* input_address_string, char* output_address_string)
{
    // Redirect stdout to /dev/null (but leave stderr open):
    close(STDOUT_FILENO);
    open("/dev/null", O_RDONLY);
    // Close stdin:
    close(STDIN_FILENO);

    int const extra_args = 15; // Including ending NULL.
    char** child_argv = malloc((sizeof(char*)) * (argc + extra_args));

    child_argv[0] = "am";
    child_argv[1] = "broadcast";
    child_argv[2] = "--user";
    child_argv[3] = "0";
    child_argv[4] = "-n";
    child_argv[5] = "com.termux.api/.TermuxApiReceiver";
    child_argv[6] = "--es";
    // Input/output are reversed for the java process (our output is its input):
    child_argv[7] = "socket_input";
    child_argv[8] = output_address_string;
    child_argv[9] = "--es";
    child_argv[10] = "socket_output";
    child_argv[11] = input_address_string;
    child_argv[12] = "--es";
    child_argv[13] = "api_method";
    child_argv[14] = argv[1];

    // Copy the remaining arguments -2 for first binary and second api name:
    memcpy(child_argv + extra_args, argv + 2, (argc-1) * sizeof(char*));

    // End with NULL:
    child_argv[argc + extra_args] = NULL;

    // Use an a executable taking care of PATH and LD_LIBRARY_PATH:
    execv("/data/data/com.termux/files/usr/bin/am", child_argv);

    perror("execv(\"/data/data/com.termux/files/usr/bin/am\")");
    exit(1);
}

void generate_uuid(char* str) {
    sprintf(str, "%x%x-%x-%x-%x-%x%x%x",
            arc4random(), arc4random(),                 // Generates a 64-bit Hex number
            (uint32_t) getpid(),                        // Generates a 32-bit Hex number
            ((arc4random() & 0x0fff) | 0x4000),         // Generates a 32-bit Hex number of the form 4xxx (4 indicates the UUID version)
            arc4random() % 0x3fff + 0x8000,             // Generates a 32-bit Hex number in the range [0x8000, 0xbfff]
            arc4random(), arc4random(), arc4random());  // Generates a 96-bit Hex number
}

// Thread function which reads from stdin and writes to socket.
void* transmit_stdin_to_socket(void* arg) {
    int output_server_socket = *((int*) arg);
    struct sockaddr_un remote_addr;
    socklen_t addrlen = sizeof(remote_addr);
    int output_client_socket = accept(output_server_socket, (struct sockaddr*) &remote_addr, &addrlen);

    ssize_t len;
    char buffer[1024];
    while (len = read(STDIN_FILENO, &buffer, sizeof(buffer)), len > 0) {
        if (write(output_client_socket, buffer, len) < 0) break;
    }
    // Close output socket on end of input:
    close(output_client_socket);
    return NULL;
}

void my_free (void *data, void *hint)
{
 free (data);
}

// Main thread function which reads from input socket and publishes on zmq socket.
void transmit_socket_to_stdout(int input_socket_fd, int pipe_read_fd) {
    ssize_t len;
    const ssize_t buffer_len = 320 * 240 * 4;
    ssize_t curr = 0;
    char *buffer = malloc(buffer_len);

    void *context = zmq_ctx_new ();
    void *publisher = zmq_socket (context, ZMQ_PUB);
    const int hwm = 50;
    zmq_setsockopt(publisher, ZMQ_SNDHWM, &hwm, sizeof(hwm)); 

    int rc = zmq_bind (publisher, "tcp://*:9999");
    if (rc == -1) {
	perror("zmq_bind()");
     }

    while ((len = read(input_socket_fd, &buffer[curr], buffer_len-curr)) > 0) {
        curr += len;
	if(curr == buffer_len){
		printf("Frame read!\n");
		zmq_msg_t msg;
		if(zmq_msg_init_data(&msg, buffer, buffer_len, my_free, NULL) != 0){
			perror("zmq_msq_init_data()");
			break;	
		}
		if(zmq_msg_send(&msg, publisher, 0) == -1){
			if(errno == EAGAIN) printf("Message dropped\n");
			else {perror("zmq_msg_send()"); break;}
		}
		buffer = malloc(buffer_len);
		curr = 0;
	}
	char buff [1];
        rc = read (pipe_read_fd, buff, 1);  // clear notifying byte
	if(rc < 0){
		if (errno == EAGAIN) { continue; }
                if (errno == EINTR) { continue; }
                perror("read()");
                break;
	}
	else if(rc == 1)
		break;
    }
    if (len < 0) perror("read()");

    printf("Cleaning up ZMQ\n");
    zmq_close(publisher);
    zmq_ctx_destroy(context);

}

int main(int argc, char** argv) {
    // Do not transform children into zombies when they terminate:
    struct sigaction sigchld_action = { .sa_handler = SIG_DFL, .sa_flags = SA_RESTART | SA_NOCLDSTOP | SA_NOCLDWAIT };
    sigaction(SIGCHLD, &sigchld_action, NULL);

    char input_address_string[100];  // This program reads from it.
    char output_address_string[100]; // This program writes to it.

    generate_uuid(input_address_string);
    generate_uuid(output_address_string);

    struct sockaddr_un input_address = { .sun_family = AF_UNIX };
    struct sockaddr_un output_address = { .sun_family = AF_UNIX };
    // Leave struct sockaddr_un.sun_path[0] as 0 and use the UUID string as abstract linux namespace:
    strncpy(&input_address.sun_path[1], input_address_string, strlen(input_address_string));
    strncpy(&output_address.sun_path[1], output_address_string, strlen(output_address_string));

    int input_server_socket = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
    if (input_server_socket == -1) { perror("socket()"); return 1; }
    int output_server_socket = socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC, 0);
    if (output_server_socket == -1) { perror("socket()"); return 1; }

    if (bind(input_server_socket, (struct sockaddr*) &input_address, sizeof(sa_family_t) + strlen(input_address_string) + 1) == -1) {
        perror("bind(input)");
        return 1;
    }
    if (bind(output_server_socket, (struct sockaddr*) &output_address, sizeof(sa_family_t) + strlen(output_address_string) + 1) == -1) {
        perror("bind(output)");
        return 1;
    }

    if (listen(input_server_socket, 1) == -1) { perror("listen()"); return 1; }
    if (listen(output_server_socket, 1) == -1) { perror("listen()"); return 1; }

    pid_t fork_result = fork();
    switch (fork_result) {
        case -1: perror("fork()"); return 1;
        case 0: exec_am_broadcast(argc, argv, input_address_string, output_address_string);
    }

    struct sockaddr_un remote_addr;
    socklen_t addrlen = sizeof(remote_addr);
    int input_client_socket = accept(input_server_socket, (struct sockaddr*) &remote_addr, &addrlen);

    pthread_t transmit_thread;
    pthread_create(&transmit_thread, NULL, transmit_stdin_to_socket, &output_server_socket);

    int pipefds[2];
    int rc = pipe(pipefds);
    if (rc != 0) {
        perror("Creating self-pipe");
        exit(1);
    }

    // Make pipe non-blocking
    for (int i = 0; i < 2; i++) {
        int flags = fcntl(pipefds[i], F_GETFL, 0);
        if (flags < 0) {
            perror ("fcntl(F_GETFL)");
            exit(1);
        }
        rc = fcntl (pipefds[i], F_SETFL, flags | O_NONBLOCK);
        if (rc != 0) {
            perror ("fcntl(F_SETFL)");
            exit(1);
        }
    }

    s_catch_signals (pipefds[1]);
    transmit_socket_to_stdout(input_client_socket, pipefds[0]);

    return 0;
}

