#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>

#include "aurras.h"

//servidor

typedef struct filtros{
    char *name;
    char *path;
    int running;
    int max;
}*Filtro;


int out = 0;
int taskNumber = 0;
int task_pid[1024];
int taskStatus[1024];
char* taskCommand[1024];
Filtro* filtrosArray;
int numberFiltros;
int fifoNumber;


//JOAO quantas linhas tem o ficheiro de configuraçoes
int howMany (){
    int pipefd[2];
    if (pipe (pipefd)== -1){
        fprintf (stderr,"erro pipe\n");
    }
    int fd = open ("etc/aurrasd.conf",O_RDONLY,0644);
    char result[10];
    if (!fork ()){
        close (pipefd[0]);
        dup2 (pipefd[1],STDOUT_FILENO);
        close (pipefd[1]);
        dup2 (fd,STDIN_FILENO);
        close (fd);
        execlp ("wc","wc","-l",NULL);
    }
    else {
        close(pipefd[1]);
        read (pipefd[0],result,10);
        wait (NULL);
    }
    return atoi (result);
}

Filtro doFiltro (char *linha, char *pathFiltros){
    Filtro filtro = malloc (sizeof (struct filtros));
    filtro->name = strdup (strtok (linha," "));
    filtro->path = strdup(pathFiltros); 
    strcat (filtro->path,strdup (strtok (NULL," ")));
    filtro->running = 0;
    filtro->max = atoi (strtok (NULL,"\n"));
    return filtro;
}


void showStatus (Filtro* filtros, int number, char *buffer){
    for (int i = 0; i < number;i++){
        Filtro f = filtros[i];
        char str[50];
        sprintf (str,"filter %s: %d/%d (running/max)\n",f->name,f->running,f->max);
        strcat (buffer,strdup (str));
    }
}


ssize_t readln(int fd, char* line, size_t size) {
    ssize_t bytes_read = read(fd, line, size);
    if(!bytes_read) return 0;

    size_t line_length = strcspn(line, "\n") + 1;
    if(bytes_read < line_length) line_length = bytes_read;
    line[line_length] = 0;
    
    lseek(fd, line_length - bytes_read, SEEK_CUR);
    return line_length;
}

Filtro* setupFiltros (int *number, char *pathFiltros, char *pathConfig){
    *number = howMany(); //JOAO para qts linhas tem o ficheiro de configuraçoes
    Filtro *array = malloc (sizeof (Filtro*) * (*number));
    //JOAO path dos filtros é aquilo eco ... 
    int fd = open (pathConfig,O_RDONLY,0644); //JOAO abrir o ficheiro de configuraçoes e vamos ler linha a lonha
    int bytes;
    char buffer[1024];
    //JOAO vamos ler cada linha o ficheiro de configuraçoes
    for (int i = 0; i < *number;i++){
        bytes = readln (fd,buffer,50);
        //JOAO vamos meter a informaçao no filtro 
        if (bytes > 1) {
            array[i] = doFiltro (buffer, pathFiltros);        }
    }
    return array;
}


void updateFiltros (char * str, int number){
    char *token;
    token = strtok(str, " ");
    while( token != NULL ) {
         for (int i = 0; i < numberFiltros;i++){
             Filtro filtro = filtrosArray[i];
             if (!strcmp(filtro->name,token)){
                 filtro->running += number;
             }
         }
       token = strtok(NULL," ");
    }
}


int canProcess (char *str){
    char *rest = strdup (str);
    char *token;
    token = strtok(rest, " ");
    int array[numberFiltros];
    for (int i = 0;i < numberFiltros;i++) array[i] = 0;
    while( token != NULL ) {
         for (int i = 0; i < numberFiltros;i++){
             Filtro filtro = filtrosArray[i];
             if (!strcmp(filtro->name,token)){
                 array[i] += 1;
             }
         }
       token = strtok(NULL," ");
    }
    int result = 1;
    for (int i = 0; i < numberFiltros;i++){
        Filtro filtro = filtrosArray[i];
        if (filtro->max < array[i]) {
            result = -1;
            break;
        }
        if (filtro->max < filtro->running + array[i]) {
            result = 0;
            break;
        }
    }
    free (rest);
    return result;
}

//JOAO espera que todos os progs estejam a executar e nao recebe mais pedidos
void sigTermHandler (int signum){
    for (int i = 0; i < taskNumber;i++){
        if (taskStatus[i] == A_EXECUTAR){
            waitpid (task_pid[i],NULL,0);
        }
    out = 1;
    }
}

//JOAO qd um processo filho morre
void sigChld_handler (int signum){
    int status;
    int pid;
    while((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0;i < taskNumber;i++){
            if (task_pid[i] == pid){
                //JOAO mete status como terminado
                taskStatus[i] = TERMINADO;
                //JOAO da update dos filtros p dizer que estes passam a estar disponiveis
                updateFiltros(taskCommand[i],-1);
                break;
            }
        }
    }
    
}


//ver se 
int check_ffmpeg (){
    if (!(fork ())){
        //ver o
        int fd = open("/dev/null", O_WRONLY);
        dup2 (fd,STDOUT_FILENO);
        close (fd);
        execlp ("which","which","ffmpeg",NULL);
    }
    else
    {
        int status;
        wait (&status);
        //Joao
        if (WEXITSTATUS (status) == 0) return 1; ////se o exit status de 0 esta certo
        else if (WEXITSTATUS (status) == 1) printf ("need ffmpeg installed\n");
    }
    return 0; // return  se nao estiver instalado
    
}

char* getFiltro (Filtro* array, int numberFiltros,char *name){
    for (int i = 0; i < numberFiltros;i++){
        Filtro f = array[i];
        if (!(strcmp (name,f->name))) return f->path;
    }
    return NULL;
}


void loop_pipe(char **cmd,char *output,char *input){
    int p[2];
    pid_t pid;
    //abre input e output
    int fd_in = open (input,O_RDONLY,0644);
    int fdout = open (output,O_CREAT |O_TRUNC |O_WRONLY,0644);
    if (fd_in == -1 || fdout == -1) return;  //caso dê erro da return e nao faz mais anda
    //JOAO while 
    while (*cmd != NULL) {
        //cria o pipe 
        if (pipe(p)) {
            fprintf(stderr, "Pipe failed.\n");
            return ;
        }
        //cria o fork (filho) 
        if (!(pid = fork ())) {
            //JOAO o nosso input passas a ser o FDIN
            dup2(fd_in, STDIN_FILENO); //set input from previous commands
            //JOAO vai ver se o comando a seguir e NULL
            //se der NULL mete o output como o ficheiro que demos
            if (cmd[1] != NULL) //if there's a command next change the output to write in pipe
                dup2(p[1],STDOUT_FILENO); 
                //JOAO senao manda p pipe de escrita
            else {
                dup2(fdout,STDOUT_FILENO);
                close (fdout);
            }
            close(p[0]); //JOAO close do pipe de leitura
            execlp(cmd[0],cmd[0],NULL); //JOAO vamos executar o comando que tiver na posiçao 0
            exit(EXIT_FAILURE);
        } else {
            wait(NULL); //JOAO pai espera
            close(p[1]); //JOAO fecha o pipe de escrita  
            fd_in = p[0];  //save the input for the next command
            cmd++;
        }
    }
}


int main(int argc, char** argv){
    if (argc != 3 ) {
        fprintf (stderr,"Invalid number of arguments");
        return 0;
    }
    //JOAO caso der 0 fechamos logo o programa
    if (!check_ffmpeg()) return 0;  //JOAO vamos começar a ver se o programa tem o ffmpeg
    filtrosArray = setupFiltros (&numberFiltros,argv[2],argv[1]);
    //JOAO vamos cirar os 2 fifos que precisamos: o cliente p servidor e servidor para o clietne
    mkfifo (CLIENTETOSERVER,0644); //comunicação do cliente para o servidor
    mkfifo (SERVERTOCLIENTE,0644); //comunicação do servidor para o cliente
    //JOAO singal - sempre que um filho morre vamos ter uma funçao que vai tratar do que 
    //acontece quado isso acontece
    signal (SIGTERM,sigTermHandler); //JOAO quando queremos que o prog acabe
    signal(SIGCHLD, sigChld_handler);
    char buffer[1024];
    //JOAO no while vai abrir o fifo do cliente servidor como leitura e vai esperar que o cliente escreva alguma coisa
    while (1){
        int fromCliente = open (CLIENTETOSERVER,O_RDONLY,0644); //JOAO espera que o cliente escreva 
        //JOAO so abre o prox quando abrirem o anterior
        int toCliente = open (SERVERTOCLIENTE,O_WRONLY,0644);
        if (out) break;
        //JOAO while server para ler o que o cliente escreveu
        while (read (fromCliente,buffer,1024)> 0){
            close (fromCliente);
            char fifo[200];
            //JOAO criar fifo exclusivo p cliente
            sprintf (fifo,"tmp/fifo%d",fifoNumber++);
            mkfifo (fifo,0644);
            //
            write(toCliente,fifo,strlen (fifo)+1);
            //JOAO fhecamos o server to clietne
            close(toCliente);
            //JOAO abrimos esse fifo
            int toPersonalCliente = open (fifo,O_WRONLY,0644);
            char outputBuffer[1024];
            outputBuffer[0] = 0;
            char *command = strdup (buffer);
            //JOAO Este filtro para o status dos filtros
            //(mostrar tasks que tao a correr e filtros que estao executados)
            //relatorio print canto inf direito
            if (!strcmp (command,"status")){
                for (int i = 0;i < taskNumber;i++){
                    if (taskStatus[i] == 0){
                        char str[200];
                        sprintf (str,"task #%d:%s\n",i,taskCommand[i]);
                        strcat(outputBuffer,strdup(str));
                    }
                }
                showStatus (filtrosArray,numberFiltros,outputBuffer);
                char strPid[20];
                sprintf (strPid,"pid: %d\n",getpid());
                strcat(outputBuffer,strPid);
                write (toPersonalCliente,outputBuffer,strlen(outputBuffer) +1);
                close (toPersonalCliente);
                unlink (fifo);
            }
            //JOAO Processamos os filtros 
            //prints relatorio sup direito e inf esquerdo
            if (!strcmp (strtok (command," "),"transform")){
                char message[] = "Pending\n";
                //TASK PENDESTE
                taskStatus[taskNumber] = PENDENTE;
                //Mand ao pending p cliente
                write (toPersonalCliente,message,strlen(message) +1);
                //damos parse do input e do output
                taskCommand[taskNumber] = strdup (buffer); 
                char *linha = strtok (NULL,"");
                char *input = strtok (linha," "); 
                char *output = strtok (NULL," ");  
                char*cmd[20]; //guardar array de strings dos filtros
                int number = 0;  
                char *token;
                //JOAO while - f1 f2 f3  ->  [pathf1, pathf2, pathf3, NULL]
                while ((token = strtok(NULL, " "))){
                    cmd[number] = getFiltro(filtrosArray,numberFiltros,token);
                    number++;
                }
                cmd[number] = NULL; //verificar quando da NULL no que é criado                
                pid_t pid;
                int result;
                //JOAO verificar se tem filtros disponiveis -> é oq faz a canProceed
                // JOAO se nao tiver vai esperar ate que tenha
                while ((result =canProcess(buffer)) == 0){
                    sleep (0.5);
                }
                //JOAO se der -1 é porque nao temos capacidade do filtro
                //da print daquela mensagem
                if (result == -1){
                    char message2[] = "O seu pedido ultrapassa o limite máximo para um filtro\nstatus para ver a disponibilidade\n";
                    write (toPersonalCliente,message2,strlen(message2) +1);
                }
                //JOAO vamos passar ao processamonto do pedido
                else{
                    //em vez de -1 passa a ser 1
                    updateFiltros(buffer,1);
                    //mandar msg ao cliente  a dizer q ta a ser processado
                    char message1[] = "Processing\n";
                    //manda a info do "processing" p cliente
                    write (toPersonalCliente,message1,strlen(message1) +1);
                    taskStatus[taskNumber] = A_EXECUTAR;
                    //vamos fazer a execuçao
                    //fazemos um fork pq queremos que a execuçao continue p prox pedido
                    if (!(pid =fork ())){
                        loop_pipe(cmd,output,input);
                        close (toPersonalCliente);
                        unlink (fifo);
                        exit(0);
                    }
                    task_pid[taskNumber] = pid;
                    taskNumber++;
                }
            }
        close (toPersonalCliente);
        }
    }
    unlink(CLIENTETOSERVER);
    unlink(SERVERTOCLIENTE);
}
