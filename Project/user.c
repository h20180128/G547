#include <stdio.h>
#include <stdlib.h> 

int main()
{
    char c[1];
    FILE *fptr1;
    int ip;
    float temp;
     
	      
   if ((fptr1 = fopen("/sys/class/hwmon/hwmon0/temp1_input", "r")) == NULL)
    {
	printf("Error!");
	exit(1);         
    }
    
    fscanf(fptr1,"%[^\n]", c);
    sscanf(c,"%d",&ip);
    temp = ip/1000.0;
    printf("Temp in degree celsius : %.1f\n", temp);
    fclose(fptr1);

}
