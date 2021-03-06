#include <stdio.h>
#include <iostream>
#include <sstream>
#include "fftutils.h"
#include "cooleyTukey.h"
#include "mpi.h"

using namespace std;

int root=0;
int initialxRange;
int initialyRange;
int initialzRange;


int main(int argc, char **argv){
  
  double startTime, endTime;

  MPI_Init(NULL,NULL);

  //get the number of processes
  int world_size=0;
  MPI_Comm_size(MPI_COMM_WORLD,&world_size);

  //get the rank
  int rankid=0;
  MPI_Comm_rank(MPI_COMM_WORLD,&rankid);
	// Get the name of the processor
  char processor_name[MPI_MAX_PROCESSOR_NAME];
  int name_len;
  MPI_Get_processor_name(processor_name, &name_len);

  MPI_Barrier(MPI_COMM_WORLD); /* IMPORTANT */
  if (rankid == root) {
    startTime = MPI_Wtime();
  }

  //Only the Master node read from configuration file
  if(rankid==root){
    
  	const char* const fName = (argc == 2) ? argv[1] : 0;

  	if (fName) {
      if (!readConfig(fName)) {
        printf("Error in config file abort");
        return 0;
      }else{
        printf("Reading the file - %s - [end]\n", fName);        
      }
           
    } else {
      printf("Config file not specified. Executing with default config");
    }

      //print configuration file
      cout << "BLOCK_SIZE = " << blockSize << endl;
      cout << "PRINT_RESULT " << print << endl;
      cout << "xRange = " << xRange << endl;
      cout << "yRange = " << yRange << endl;
      cout << "zRange = " << zRange << endl;

      //print selected algorithm
      if (fftAlgo==2){
      	cout << "FFT algorithm = COOLEY_TUKEY" << endl;        
      }else{
      	cout << "Unknow FFT algorithm" << endl;
      }
    }

    // Initialize the vars (with the data coming from the config file) to share among all processes
    int config_settings[4];
    if (rankid==root){
      config_settings[0]=xRange;
      config_settings[1]=yRange;
      config_settings[2]=zRange;
      config_settings[3]=print;
    }
    
    MPI_Bcast(&config_settings, 4, MPI_INT, root, MPI_COMM_WORLD);

    if (rankid!=root){
      xRange=config_settings[0];
      yRange=config_settings[1];
      zRange=config_settings[2];
      _show_result= config_settings[3];
    }

    if(rankid==root){
      printf("Hello world from processor %s, rank %d out of %d processors- Show_Result %d. THE MASTER\n", processor_name, rankid, world_size,_show_result);
    }else{
      printf("Hello world from processor %s, rank %d out of %d processors- Show_Result %d. SLAVE\n", processor_name, rankid, world_size,_show_result);

    }




    const unsigned n = xRange;
    const unsigned size = xRange*yRange*zRange;
    int ASPAN = xRange*yRange*zRange;
    int zBar, yBar, xBar;
    int full3dfft=1;
    
    int FFT_type = 0;
    unsigned matrix_size=size; // matrix dimension sizeOnCPU
    int num_elements_per_proc=size; //matrix dimension

    initialxRange = xRange;
    initialyRange = yRange;
    initialzRange = zRange;

    if (zRange > 1) {
      zBar = xRange; 
      yBar = yRange; 
      xBar = zRange;
    }
    else { 
      zBar = zRange; 
      yBar = xRange; 
      xBar = yRange;
    }



    //...and allocate memory on root node and load input data from file
    if (rankid==root){
      if (!initExecution(size, n)) {
        return false;
      }      
    }
    //buffer to receive data from scatter
    double *recv_hraVec_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
    double *recv_hiaVec_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);

    if(full3dfft==1){
      FFT_type = 0; //Forward FFT
      //----------------------------(2. aVec XX,XY,XZ,YY,YZ,ZZ [start])----------------------------------

      //
      double startTimeScatter,endTimeScatter;
      if (rankid==root){
        startTimeScatter=MPI_Wtime();   
      }
      MPI_Scatter(hraVec, num_elements_per_proc, MPI_DOUBLE, recv_hraVec_buffer, num_elements_per_proc, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Scatter(hiaVec, num_elements_per_proc, MPI_DOUBLE, recv_hiaVec_buffer, num_elements_per_proc, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      

      //stringstream msg;
      //msg << "recv_hraVec_buffer recv_hiaVec_buffer from rankid " << rankid;
      //printMeInfo(msg.str(),0,recv_hraVec_buffer, recv_hiaVec_buffer, zRange, yRange, xRange, 0*ASPAN );

      //local buffer
      //TODO only 
      double *local_hrRaVec_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
      double *local_hiRaVec_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
      
      if (_show_result){
        printf("Process:%d aVec cooleyTukeyCpu3DFFT with n:%d matrix_size:%d FFT_type:%d xRange:%d yRange:%d zRange:%d \n",rankid,n,matrix_size,FFT_type,xRange,yRange,zRange);
      }     
      
      cooleyTukeyCpu3DFFT(0, n, matrix_size,recv_hraVec_buffer,recv_hiaVec_buffer,local_hrRaVec_buffer,local_hiRaVec_buffer,0,_show_result,FFT_type,xRange,yRange,zRange);

      //only the root has allocated the memory for hrRaVec and hiRaVec
      MPI_Gather(local_hrRaVec_buffer, num_elements_per_proc, MPI_DOUBLE, hrRaVec, num_elements_per_proc, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      MPI_Gather(local_hiRaVec_buffer, num_elements_per_proc, MPI_DOUBLE, hiRaVec, num_elements_per_proc, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      if (rankid=root){
        endTimeScatter= MPI_Wtime();
        printf("Total Runtime in second (before scatter -> after gather on root node) = %f\n", endTimeScatter-startTimeScatter);
      }

      //----------------------------(2. aVec XX,XY,XZ,YY,YZ,ZZ [Ends])----------------------------------
      //all the slave must have complete

      //----------------------------(3 mVecI [Starts])----------------------------------
      if (rankid==root){
        int dest=1;

        if (_show_result){
          printf("Sending the mVecI data from root to dest: %d ...\n", dest);
        }

        MPI_Send(hrmVecI, matrix_size, MPI_DOUBLE, dest, 0, MPI_COMM_WORLD);
        MPI_Send(himVecI, matrix_size, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);

        if (_show_result){
          printf("Sent the mVecI data from root to dest: %d end\n", dest);
        }
      }
      if (rankid==1){
        double *recv_hrmVecI_buffer=(double*)malloc(sizeof(double)*matrix_size);
        double *recv_himVecI_buffer=(double*)malloc(sizeof(double)*matrix_size);
        
        double *local_hrRmVecI_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
        double *local_hiRmVecI_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
        if (_show_result){
          printf("Process 1: Receiving the mVecI data from root...\n");
        }
        MPI_Recv(recv_hrmVecI_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(recv_himVecI_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        if (_show_result){
          printf("Process 1: Received the mVecI data from root [end]\n");
          printf("Process 1: Processing with ct3dfft mVecI data ...\n");
        }

        
        cooleyTukeyCpu3DFFT(0, n, matrix_size,recv_hrmVecI_buffer,recv_himVecI_buffer,local_hrRmVecI_buffer,local_hiRmVecI_buffer,0,_show_result,FFT_type,xRange,yRange,zRange);
        if (_show_result){
          printf("Process 1: Processed with ct3dfft mVecI data \n");
          //send the data processed back to root   
          printf("Sending the mVecI data processed from process 1 to root...\n");     
        }
        MPI_Send(local_hrRmVecI_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD);
        MPI_Send(local_hiRmVecI_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD);

        if (_show_result){
          printf("Sent the mVecI data processed from process 1 to root end\n");
        }
      }
      //----------------------------(3 mVecI [Ends])----------------------------------



      //----------------------------(4 mVecJ [Starts])----------------------------------
      if (rankid==root){
        int dest=2;
        if (_show_result){
          printf("Sending the mVecJ data from root to dest: %d ...\n", dest);
        }
        MPI_Send(hrmVecJ, matrix_size, MPI_DOUBLE, dest, 0, MPI_COMM_WORLD);
        MPI_Send(himVecJ, matrix_size, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);
        if (_show_result){
          printf("Sent the mVecJ data from root to dest: %d end\n", dest);
        }
      }
      if (rankid==2){
        double *recv_hrmVecJ_buffer=(double*)malloc(sizeof(double)*matrix_size);
        double *recv_himVecJ_buffer=(double*)malloc(sizeof(double)*matrix_size);
        
        double *local_hrRmVecJ_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
        double *local_hiRmVecJ_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
        if (_show_result){
          printf("Process 2: Receiving the mVecJ data from root...\n");
        }
        MPI_Recv(recv_hrmVecJ_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(recv_himVecJ_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        if (_show_result){
          printf("Process 2: Received the mVecJ data from root [end]\n");
          printf("Process 2: Processing with ct3dfft mVecJ data ...\n");
        }
        cooleyTukeyCpu3DFFT(0, n, matrix_size,recv_hrmVecJ_buffer,recv_himVecJ_buffer,local_hrRmVecJ_buffer,local_hiRmVecJ_buffer,0,_show_result,FFT_type,xRange,yRange,zRange);
        
        if (_show_result){
          printf("Process 2: Processed with ct3dfft mVecJ data \n");
          //send the data processed back to root   
          printf("Sending the mVecJ data processed from process 2 to root...\n");     
        }

        MPI_Send(local_hrRmVecJ_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD);
        MPI_Send(local_hiRmVecJ_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD);
        if (_show_result){
          printf("Sent the mVecJ data processed from process 2 to root end\n");
        }
      }

      //----------------------------(4 mVecJ [Ends])----------------------------------




      //----------------------------(5 mVecK [Starts])----------------------------------
      if (rankid==root){
        if (_show_result){
          printf("Process 0: Processing with ct3dfft mVecK data ...\n");
        }

        cooleyTukeyCpu3DFFT(0, n, matrix_size,hrmVecK,himVecK,hrRmVecK,hiRmVecK,0,_show_result,FFT_type,xRange,yRange,zRange);
        
        if (_show_result){
          printf("Process 0: Processed with ct3dfft mVecK data \n");
        }
        

        /*
        int dest=3;
        printf("Sending the mVecK data from root to dest: %d ...\n", dest);
        MPI_Send(hrmVecK, matrix_size, MPI_DOUBLE, dest, 0, MPI_COMM_WORLD);
        MPI_Send(himVecK, matrix_size, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);
        printf("Sent the mVecK data from root to dest: %d end\n", dest);*/
      }
      /*
      if (rankid==3){
        double *recv_hrmVecK_buffer=(double*)malloc(sizeof(double)*matrix_size);
        double *recv_himVecK_buffer=(double*)malloc(sizeof(double)*matrix_size);
        
        double *local_hrRmVecK_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
        double *local_hiRmVecK_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
        
        printf("Process 3: Receiving the mVecJ data from root...\n");
        MPI_Recv(recv_hrmVecK_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(recv_himVecK_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        printf("Process 3: Received the mVecK data from root [end]\n");

        printf("Process 3: Processing with ct3dfft mVecK data ...\n");
        cooleyTukeyCpu3DFFT(0, n, matrix_size,recv_hrmVecK_buffer,recv_himVecK_buffer,local_hrRmVecK_buffer,local_hiRmVecK_buffer,0,show_result,FFT_type,xRange,yRange,zRange);
        printf("Process 3: Processed with ct3dfft mVecK data \n");
        //send the data processed back to root   
        printf("Sending the mVecK data processed from process 3 to root...\n");     
        MPI_Send(local_hrRmVecK_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD);
        MPI_Send(local_hiRmVecK_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD);
        printf("Sent the mVecK data processed from process 3 to root end\n");
      }*/
      //----------------------------(5 mVecK [Ends])----------------------------------
       //check !!!!!!!! 
      
      if (rankid==root){
        if (_show_result){
          printf("Root: Receiving the mVecI data from process 1\n");
        }

        //mVecI
        MPI_Recv(hrRmVecI, matrix_size, MPI_DOUBLE, 1, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(hiRmVecI, matrix_size, MPI_DOUBLE, 1, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        if (_show_result){
          printf("Root: Receved the mVecI data from process 1\n");        
          printf("Root: Receiving the mVecJ data from process 2\n");
        }

        //mVecJ
        MPI_Recv(hrRmVecJ, matrix_size, MPI_DOUBLE, 2, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(hiRmVecJ, matrix_size, MPI_DOUBLE, 2, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        if (_show_result){
          printf("Root: Receved the mVecJ data from process 2\n");
        }
      }

    } //end if full3dfft==1
    
    //check mpi_barrier //MPI_Barrier(MPI_COMM_WORLD);

    
    if (rankid==root){
      convolveCPU(0,ASPAN);
      if (_show_result){
        printf("Hvec I inverse \n");
        printf("XRange=:%d\t YRange=:%d\t ZRange=:%d\t  \n", xRange,yRange,zRange);  
      }
    }

    
    FFT_type = 1; //Inverse FFT

    //----------------------------(6 hVecI [Starts])----------------------------------
    if (rankid==root){
      int dest=3;
      if (_show_result){
        printf("Sending the hVecI data from root to dest: %d ...\n", dest);
      }

      MPI_Send(hrhVecI, matrix_size, MPI_DOUBLE, dest, 0, MPI_COMM_WORLD);
      MPI_Send(hihVecI, matrix_size, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);

      if (_show_result){
        printf("Sent the hVecI data from root to dest: %d end\n", dest);
      }
    }
    if (rankid==3){
      double *recv_hrhVecI_buffer=(double*)malloc(sizeof(double)*matrix_size);
      double *recv_hihVecI_buffer=(double*)malloc(sizeof(double)*matrix_size);
      
      double *local_hrRhVecI_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
      double *local_hiRhVecI_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
      
      if (_show_result){
        printf("Process 3: Receiving the hVecI data from root...\n");
      }

      MPI_Recv(recv_hrhVecI_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Recv(recv_hihVecI_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      
      if (_show_result){
        printf("Process 3: Received the hVecI data from root [end]\n");      
        printf("Process 3: Processing with ct3dfft hVecI data ...\n");
      }

      cooleyTukeyCpu3DFFT(0, n, matrix_size,recv_hrhVecI_buffer,recv_hihVecI_buffer,local_hrRhVecI_buffer,local_hiRhVecI_buffer,0,_show_result,FFT_type,xRange,yRange,zRange);
      if (_show_result){
        printf("Process 3: Processed with ct3dfft hVecI data \n");      
        //send the data processed back to root   
        printf("Sending the hVecI data processed from process 3 to root...\n");     
      }

      MPI_Send(local_hrRhVecI_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD);
      MPI_Send(local_hiRhVecI_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD);
      
      if (_show_result){
        printf("Sent the hVecI data processed from process 3 to root end\n");
      }
    }

    //----------------------------(6 hVecI [Ends])----------------------------------
    
    //----------------------------(6 hVecJ [Starts])----------------------------------
    if (rankid==root){
      if (zRange > 1) {
        zBar = xRange; yBar = yRange; xBar = zRange;
        xRange=zRange; zRange=zBar; yRange=yBar; 
      }
      else { 
        zBar = zRange; yBar = xRange; xBar = yRange;
        zRange=zBar; yRange=xRange; xRange=xBar;
      }
      if (_show_result){
        printf("Hvec J inverse \n");
        printf("XRange=:%d\t YRange=:%d\t ZRange=:%d\t  \n", xRange,yRange,zRange);
      }

      int dest=4;
      if (_show_result){
        printf("Sending the hVecJ data from root to dest: %d ...\n", dest);
      }

      MPI_Send(hrhVecJ, matrix_size, MPI_DOUBLE, dest, 0, MPI_COMM_WORLD);
      MPI_Send(hihVecJ, matrix_size, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);
      
      if (_show_result){
        printf("Sent the hVecJ data from root to dest: %d end\n", dest);
      }
    }
    if (rankid==4){
      double *recv_hrhVecJ_buffer=(double*)malloc(sizeof(double)*matrix_size);
      double *recv_hihVecJ_buffer=(double*)malloc(sizeof(double)*matrix_size);
      
      double *local_hrRhVecJ_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
      double *local_hiRhVecJ_buffer=(double*)malloc(sizeof(double)*num_elements_per_proc);
      
      if (_show_result){
        printf("Process 4: Receiving the hVecJ data from root...\n");
      }

      MPI_Recv(recv_hrhVecJ_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Recv(recv_hihVecJ_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      if (_show_result){
        printf("Process 4: Received the hVecJ data from root [end]\n");
        printf("Process 4: Processing with ct3dfft hVecJ data ...\n");
      }

      cooleyTukeyCpu3DFFT(0, n, matrix_size,recv_hrhVecJ_buffer,recv_hihVecJ_buffer,local_hrRhVecJ_buffer,local_hiRhVecJ_buffer,0,_show_result,FFT_type,xRange,yRange,zRange);
      
      if (_show_result){
        printf("Process 4: Processed with ct3dfft hVecI data \n");
        //send the data processed back to root   
        printf("Sending the hVecJ data processed from process 4 to root...\n");     
      }
      MPI_Send(local_hrRhVecJ_buffer, matrix_size, MPI_DOUBLE, root, 0, MPI_COMM_WORLD);
      MPI_Send(local_hiRhVecJ_buffer, matrix_size, MPI_DOUBLE, root, 1, MPI_COMM_WORLD);
      if (_show_result){
        printf("Sent the hVecJ data processed from process 4 to root end\n");
      }
    }

    //----------------------------(6 hVecJ [Ends])----------------------------------
    
    //----------------------------(6 hVecK [Starts])----------------------------------
    //compute on root node
    if (rankid==root){
      if (zRange > 1) {
        zBar = xRange; yBar = yRange; xBar = zRange;
        xRange=zRange; zRange=zBar; yRange=yBar; 
      }
      else { 
        zBar = zRange; yBar = xRange; xBar = yRange;
        zRange=zBar; yRange=xRange; xRange=xBar;
      }
      if (_show_result){
        printf("Hvec K inverse \n");
        printf("XRange=:%d\t YRange=:%d\t ZRange=:%d\t  \n", xRange,yRange,zRange);      
        printf("Process 0: Processing with ct3dfft hVecK data ...\n");
      }
      cooleyTukeyCpu3DFFT(0, n, matrix_size,hrhVecK,hihVecK,hrRhVecK,hiRhVecK,0,_show_result,FFT_type,xRange,yRange,zRange);
      if (_show_result){
        printf("Process 0: Processed with ct3dfft hVecK data \n");
      }

    }
    //----------------------------(6 hVecK [Ends])----------------------------------

    //Collect hVec processed data on root node

    if (rankid==root){
      if (_show_result){
        printf("Root: Receiving the hVecI data from process 3\n");
      }

      //mVecI
      MPI_Recv(hrRhVecI, matrix_size, MPI_DOUBLE, 3, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Recv(hiRhVecI, matrix_size, MPI_DOUBLE, 3, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      if (_show_result){
        printf("Root: Receved the hVecI data from process 3\n");      
        printf("Root: Receiving the hVecJ data from process 4\n");
      }
      //mVecJ
      MPI_Recv(hrRhVecJ, matrix_size, MPI_DOUBLE, 4, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      MPI_Recv(hiRhVecJ, matrix_size, MPI_DOUBLE, 4, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      if (_show_result){
        printf("Root: Receved the hVecJ data from process 4\n");
      }
    }

    //Collect hVec processed data on root node [ends]

    MPI_Barrier(MPI_COMM_WORLD);
    if (rankid == root) {
      endTime = MPI_Wtime();
    }
    
    MPI_Finalize();

    if (rankid == root) { /* use time on master node */
      printf("--------------------------------------------------------ENDS------------------------------------\n");
      printf("Total Runtime in second = %f\n", endTime-startTime);
    }




}
