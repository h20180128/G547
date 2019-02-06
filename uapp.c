#include <stdio.h> 
#include<stdlib.h>
#include<fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>
#define adxl_x "/dev/adxl_x"
#define adxl_y "/dev/adxl_y"
#define adxl_z "/dev/adxl_z"

int main()

{ 
  int fd,n;
  uint16_t y;
do{
  printf("1 xaxis \n2 yaxis \n3 zaxis\n9 exit\n");
  
  scanf("%d",&n);
  
  if(n==1)
  {
  fd=open(adxl_x, O_RDWR);
  
  if(fd  == -1)
  {
  
    printf("Locked\n",adxl_x);
    exit(-1);
  }
  
  read(fd,&y,2);
  printf("X axis is %d\n",y);
  }
  else if(n==2)
  {
  fd=open(adxl_y , O_RDWR);
  if(fd  == -1)
  {
  
    printf("Locked\n",adxl_y);
    exit(-1);
  }
  
  read(fd,&y,2);
  printf("Y axis is %d\n",y);
  }
  else if(n==3)
  {
  fd=open(adxl_z, O_RDWR);
  
  if(fd  == -1)
  {
  
    printf("Open failed\n",adxl_z);
    exit(-1);
  }
  
  read(fd,&y,2);
  printf("Z axis  is %d\n",y);
  }

else if(n==9)
;
else
printf("Allowed options : 1,2,3,9\n");
}while(n!=9);
  close(fd);
  
  
  return 0;
}
