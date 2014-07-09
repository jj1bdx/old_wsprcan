// Andrew Greensted - Feb 2010
// http://www.labbookpages.co.uk
// Version 1

#include <stdio.h>
#include <stdlib.h>
#include </opt/local/include/sndfile.h>
#include <math.h>
#include <strings.h>
#include "/opt/local/include/fftw3.h"


unsigned char pr3[162]=
{1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,
    0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,
    0,0,0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
    1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,
    0,0,1,0,1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,
    0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,
    0,1,0,0,0,1,1,1,0,0,0,0,0,1,0,1,0,0,1,1,
    0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,
    0,0};

unsigned long readwavfile(int argc, char *argv[], float **buffer )
{

    printf("k9an-wspr\n");
	if (argc != 2) {
		fprintf(stderr, "Expecting wav file as argument\n");
		return 1;
	}
    
// Open sound file
    SF_INFO sndInfo;
    SNDFILE *sndFile = sf_open(argv[1], SFM_READ, &sndInfo);
    if (sndFile == NULL) {
        fprintf(stderr, "Error reading source file '%s': %s\n", argv[1], sf_strerror(sndFile));
    return 1;
    }

// Check format - 16bit PCM
    if (sndInfo.format != (SF_FORMAT_WAV | SF_FORMAT_PCM_16)) {
        fprintf(stderr, "Input should be 16bit Wav\n");
        sf_close(sndFile);
        return 1;
    }

// Check channels - mono
    if (sndInfo.channels != 1) {
        fprintf(stderr, "Wrong number of channels\n");
        sf_close(sndFile);
        return 1;
    }

// Allocate memory
    *buffer = malloc(sndInfo.frames * 2 * sizeof(float));
    if (buffer == NULL) {
        fprintf(stderr, "Could not allocate memory for file\n");
        sf_close(sndFile);
        return 1;
    }

// Load data
    unsigned long nframes = sf_readf_float(sndFile, *buffer, sndInfo.frames);

// Check correct number of samples loaded
    if (nframes != sndInfo.frames) {
        fprintf(stderr, "Did not read enough frames for source\n");
        sf_close(sndFile);
        free(buffer);
        return 1;
    }

// Output Info
/*    printf("Read %ld frames from %s, Sample rate: %d, Length: %fs\n",
           numFrames, argv[1], sndInfo.samplerate, (float)numFrames/sndInfo.samplerate);
 */

    printf("Read %ld frames from %s \n",nframes, argv[1]);

    sf_close(sndFile);

    return nframes;
}

void getStats(double *id, double *qd, long np, double *mi, double *mq, double *mi2, double *mq2, double *miq)
{
    double sumi=0.0;
    double sumq=0.0;
    double sumi2=0.0;
    double sumq2=0.0;
    double sumiq=0.0;
    int i;
    
    for (i=0; i<np; i++) {
        sumi=sumi+id[i];
        sumi2=sumi2+id[i]*id[i];
        sumq=sumq+qd[i];
        sumq2=sumq2+qd[i]*qd[i];
        sumiq=sumiq+id[i]*qd[i];
    }
    *mi=sumi/np;
    *mq=sumq/np;
    *mi2=sumi2/np;
    *mq2=sumq2/np;
    *miq=sumiq/np;
}

void sync_and_demodulate(
double *id,
double *qd,
long np,
unsigned char *symbols,
float *f1,
float fstep,
int *shift1,
int lagmin, int lagmax, int lagstep,
float *drift1,
float *sync,
int mode)
{
    // mode is the last argument:
    // 0 no frequency or drift search. find best time lag.
    // 1 no time lag or drift search. find best frequency.
    // 2 no frequency or time lag search. find best drift.

    float dt=1.0/375.0, df=375.0/256.0;
    long int i, j, k;
    double pi=4.*atan(1.0);
    float f0,fp,ss;
    long int lag;
    
    double
    i0[162],q0[162],
    i1[162],q1[162],
    i2[162],q2[162],
    i3[162],q3[162];
    
    double p0,p1,p2,p3,cmet,totp,covmax,pmax, phase, fac;
    double
    c0[256],s0[256],
    c1[256],s1[256],
    c2[256],s2[256],
    c3[256],s3[256];
    double
    dphi0, cdphi0, sdphi0,
    dphi1, cdphi1, sdphi1,
    dphi2, cdphi2, sdphi2,
    dphi3, cdphi3, sdphi3;
    float fsymb[162];
    
    df=375.0/256.0;
    int best_shift = 0, ifreq;
    
    covmax=-1e30;
    pmax=-1e30;

    int ifmin, ifmax;

// mode is the last argument:
// 0 no frequency or drift search. find best time lag.
// 1 no time lag or drift search. find best frequency.
// 2 no frequency or time lag search. find best drift.
    if( mode == 0 ) {
        ifmin=0;
        ifmax=0;
        fstep=0.0;
        f0=*f1;
    }
    if( mode == 1 ) {
        lagmin=*shift1;
        lagmax=*shift1;
        ifmin=-5;
        ifmax=5;
        f0=*f1;
    }
    if( mode == 2 ) {
        best_shift = *shift1;
        f0=*f1;
    }

    if( mode != 2 ) {
    for(ifreq=ifmin; ifreq<=ifmax; ifreq++)
    {
        f0=*f1+ifreq*fstep;
// search lag range
        
        for(lag=lagmin; lag<=lagmax; lag=lag+lagstep)
        {
            ss=0;
            totp=0;
            for (i=0; i<162; i++)
            {
                fp = f0 + (*drift1/2.0)*(i-81)/81.0;
                
                dphi0=2*pi*(fp-1.5*df)*dt;
                cdphi0=cos(dphi0);
                sdphi0=sin(dphi0);
                dphi1=2*pi*(fp-0.5*df)*dt;
                cdphi1=cos(dphi1);
                sdphi1=sin(dphi1);
                dphi2=2*pi*(fp+0.5*df)*dt;
                cdphi2=cos(dphi2);
                sdphi2=sin(dphi2);
                dphi3=2*pi*(fp+1.5*df)*dt;
                cdphi3=cos(dphi3);
                sdphi3=sin(dphi3);
                
                c0[0]=1;
                s0[0]=0;
                c1[0]=1;
                s1[0]=0;
                c2[0]=1;
                s2[0]=0;
                c3[0]=1;
                s3[0]=0;
                
                for (j=1; j<256; j++) {
                    c0[j]=c0[j-1]*cdphi0-s0[j-1]*sdphi0;
                    s0[j]=c0[j-1]*sdphi0+s0[j-1]*cdphi0;
                    c1[j]=c1[j-1]*cdphi1-s1[j-1]*sdphi1;
                    s1[j]=c1[j-1]*sdphi1+s1[j-1]*cdphi1;
                    c2[j]=c2[j-1]*cdphi2-s2[j-1]*sdphi2;
                    s2[j]=c2[j-1]*sdphi2+s2[j-1]*cdphi2;
                    c3[j]=c3[j-1]*cdphi3-s3[j-1]*sdphi3;
                    s3[j]=c3[j-1]*sdphi3+s3[j-1]*cdphi3;
                }
                
                i0[i]=0.0;
                q0[i]=0.0;
                i1[i]=0.0;
                q1[i]=0.0;
                i2[i]=0.0;
                q2[i]=0.0;
                i3[i]=0.0;
                q3[i]=0.0;
 
                for (j=0; j<256; j++)
                {
                    k=lag+i*256+j;
                    if( (k>0) & (k<np) ) {
                    i0[i]=i0[i]+id[k]*c0[j]+qd[k]*s0[j];
                    q0[i]=q0[i]-id[k]*s0[j]+qd[k]*c0[j];
                    i1[i]=i1[i]+id[k]*c1[j]+qd[k]*s1[j];
                    q1[i]=q1[i]-id[k]*s1[j]+qd[k]*c1[j];
                    i2[i]=i2[i]+id[k]*c2[j]+qd[k]*s2[j];
                    q2[i]=q2[i]-id[k]*s2[j]+qd[k]*c2[j];
                    i3[i]=i3[i]+id[k]*c3[j]+qd[k]*s3[j];
                    q3[i]=q3[i]-id[k]*s3[j]+qd[k]*c3[j];
                    }
                }
                p0=i0[i]*i0[i]+q0[i]*q0[i];
                p1=i1[i]*i1[i]+q1[i]*q1[i];
                p2=i2[i]*i2[i]+q2[i]*q2[i];
                p3=i3[i]*i3[i]+q3[i]*q3[i];
            
                totp=totp+p0+p1+p2+p3;
                cmet=(p1+p3)-(p0+p2);
                ss=ss+cmet*(2*pr3[i]-1);
            }
            
            if( ss > covmax ) {
                covmax=ss;
                pmax=totp;

                best_shift=lag;
                
                pmax=totp;
                *f1=f0;
            }
        } // lag loop
        

    } //freq loop
    *sync=covmax/pmax;
    *shift1=best_shift;
        return;
    } //if not mode 2
    
    if( mode == 2 )
    {
//    printf("fbest: %f t0: %f\n", *f1, best_shift*dt);

        for (i=0; i<162; i++)
        {
            i0[i]=0.0;
            q0[i]=0.0;
            i1[i]=0.0;
            q1[i]=0.0;
            i2[i]=0.0;
            q2[i]=0.0;
            i3[i]=0.0;
            q3[i]=0.0;
            
            fp=f0+(*drift1/2.0)*(i-81.0)/81.0;
            for (j=0; j<256; j++)
            {
                k=best_shift+i*256+j;
                phase=2*pi*(fp-1.5*df)*k*dt;
                i0[i]=i0[i]+id[k]*cos(phase)+qd[k]*sin(phase);
                q0[i]=q0[i]-id[k]*sin(phase)+qd[k]*cos(phase);
                phase=2*pi*(fp-0.5*df)*k*dt;
                i1[i]=i1[i]+id[k]*cos(phase)+qd[k]*sin(phase);
                q1[i]=q1[i]-id[k]*sin(phase)+qd[k]*cos(phase);
                phase=2*pi*(fp+0.5*df)*k*dt;
                i2[i]=i2[i]+id[k]*cos(phase)+qd[k]*sin(phase);
                q2[i]=q2[i]-id[k]*sin(phase)+qd[k]*cos(phase);
                phase=2*pi*(fp+1.5*df)*k*dt;
                i3[i]=i3[i]+id[k]*cos(phase)+qd[k]*sin(phase);
                q3[i]=q3[i]-id[k]*sin(phase)+qd[k]*cos(phase);
                
            }
            
            p0=i0[i]*i0[i]+q0[i]*q0[i];
            p1=i1[i]*i1[i]+q1[i]*q1[i];
            p2=i2[i]*i2[i]+q2[i]*q2[i];
            p3=i3[i]*i3[i]+q3[i]*q3[i];

            
            if( pr3[i] == 1 )
            {
                fsymb[i]=(p3-p1);
            } else {
                fsymb[i]=(p2-p0);
            }
            
        }
        float fsum=0.0, f2sum=0.0;
        for (i=0; i<162; i++) {
            fsum=fsum+fsymb[i]/162.0;
            f2sum=f2sum+fsymb[i]*fsymb[i]/162.0;
//            printf("%d %f\n",i,fsymb[i]);
        }
        fac=sqrt(f2sum-fsum*fsum);
        for (i=0; i<162; i++) {
            fsymb[i]=128*fsymb[i]/fac;
            if( fsymb[i] > 127)
                fsymb[i]=127.0;
            if( fsymb[i] < -128 )
                fsymb[i]=-128.0;
            symbols[i]=fsymb[i]+128;
//            printf("symb: %lu %5.1f\n",i, fsymb[i]);
        }
    }
}

void unpack50( signed char *dat, int32_t *n1, int32_t *n2 )
{
    int32_t i,i4;
    
    i=dat[0];
    i4=i&255;
    *n1=i4<<20;
    
    i=dat[1];
    i4=i&255;
    *n1=*n1+(i4<<12);
    
    i=dat[2];
    i4=i&255;
    *n1=*n1+(i4<<4);
    
    i=dat[3];
    i4=i&255;
    *n1=*n1+((i4>>4)&15);
    *n2=(i4&15)<<18;
    
    i=dat[4];
    i4=i&255;
    *n2=*n2+(i4<<10);

    i=dat[5];
    i4=i&255;
    *n2=*n2+(i4<<2);
    
    i=dat[6];
    i4=i&255;
    *n2=*n2+((i4>>6)&3);
}

void unpackcall( int32_t ncall, char *call )
{
    char c[]={'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',' '};
    int32_t n;
    int i;
    
    n=ncall;
    
    if (n < 262177560 ) {
        i=n%27+10;
        call[5]=c[i];
        n=n/27;
        i=n%27+10;
        call[4]=c[i];
        n=n/27;
        i=n%27+10;
        call[3]=c[i];
        n=n/27;
        i=n%10;
        call[2]=c[i];
        n=n/10;
        i=n%36;
        call[1]=c[i];
        n=n/36;
        i=n;
        call[0]=c[i];
    }
    
}

void unpackgrid( int32_t ngrid, char *grid)
{
    char c[]={'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',' '};
    int dlat, dlong;
    
    ngrid=ngrid>>7;
    if( ngrid < 32400 ) {
        dlat=(ngrid%180)-90;
        dlong=(ngrid/180)*2 - 180 + 2;
        if( dlong < -180 )
            dlong=dlong+360;
        if( dlong > 180 )
            dlong=dlong+360;
        int nlong = 60.0*(180.0-dlong)/5.0;
        int n1 = nlong/240;
        int n2 = (nlong - 240*n1)/24;
        int n3 = nlong -40*n1 - 24*n2;
        grid[0] = c[10+n1];
        grid[2]=  c[n2];

        int nlat = 60.0*(dlat+90)/2.5;
        n1 = nlat/240;
        n2 = (nlat-240*n1)/24;
        n3 = nlong - 240*n1 - 24*n2;
        grid[1]=c[10+n1];
        grid[3]=c[n2];
    } else {
        strcpy(grid,"XXXX");
    }
}

void deinterleave(unsigned char *sym)
{
    unsigned char tmp[162];
    unsigned char p, i, j;
    
    p=0;
    i=0;
    while (p<162) {
        j=((i * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
        if (j < 162 ) {
            tmp[p]=sym[j];
            p=p+1;
        }
        i=i+1;
    }
    for (i=0; i<162; i++)
        sym[i]=tmp[i];
}


#include <complex.h>
#include "/opt/local/include/fftw3.h"


// used by qsort
int floatcomp(const void* elem1, const void* elem2)
{
    if(*(const float*)elem1 < *(const float*)elem2)
        return -1;
    return *(const float*)elem1 > *(const float*)elem2;
}

int main(int argc, char *argv[])
{
	double mebn0,mesn0,mnoise;
    int i,j,k;
    float *buffer;
    double *idat, *qdat;
    double mi, mq, mi2, mq2, miq;
    unsigned char *symbols, *decdata;
    signed char message[]={-9,13,-35,123,57,-39,64,0,0,0,0};
    FILE *fp;
    int32_t n1,n2;
    char *callsign,*grid;

    int mettab[2][256]={
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   4,
    +   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,
    +   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,
    +   3,   3,   3,   3,   3,   3,   3,   3,   3,   2,
    +   2,   2,   2,   2,   1,   1,   1,   1,   0,   0,
    +  -1,  -1,  -1,  -2,  -2,  -3,  -4,  -4,  -5,  -6,
    +  -7,  -7,  -8,  -9, -10, -11, -12, -12, -13, -14,
    + -15, -16, -17, -17, -18, -19, -20, -21, -22, -22,
    + -23, -24, -25, -26, -26, -27, -28, -29, -30, -30,
    + -31, -32, -33, -33, -34, -35, -36, -36, -37, -38,
    + -38, -39, -40, -41, -41, -42, -43, -43, -44, -45,
    + -45, -46, -47, -47, -48, -49, -49, -50, -51, -51,
    + -52, -53, -53, -54, -54, -55, -56, -56, -57, -57,
    + -58, -59, -59, -60, -60, -61, -62, -62, -62, -63,
    + -64, -64, -65, -65, -66, -67, -67, -67, -68, -69,
    + -69, -70, -70, -71, -72, -72, -72, -72, -73, -74,
    + -75, -75, -75, -77, -76, -76, -78, -78, -80, -81,
    + -80, -79, -83, -82, -81, -82, -82, -83, -84, -84,
    + -84, -87, -86, -87, -88, -89, -89, -89, -88, -87,
    + -86, -87, -84, -84, -84, -83, -82, -82, -81, -82,
    + -83, -79, -80, -81, -80, -78, -78, -76, -76, -77,
    + -75, -75, -75, -74, -73, -72, -72, -72, -72, -71,
    + -70, -70, -69, -69, -68, -67, -67, -67, -66, -65,
    + -65, -64, -64, -63, -62, -62, -62, -61, -60, -60,
    + -59, -59, -58, -57, -57, -56, -56, -55, -54, -54,
    + -53, -53, -52, -51, -51, -50, -49, -49, -48, -47,
    + -47, -46, -45, -45, -44, -43, -43, -42, -41, -41,
    + -40, -39, -38, -38, -37, -36, -36, -35, -34, -33,
    + -33, -32, -31, -30, -30, -29, -28, -27, -26, -26,
    + -25, -24, -23, -22, -22, -21, -20, -19, -18, -17,
    + -17, -16, -15, -14, -13, -12, -12, -11, -10,  -9,
    +  -8,  -7,  -7,  -6,  -5,  -4,  -4,  -3,  -2,  -2,
    +  -1,  -1,  -1,   0,   0,   1,   1,   1,   1,   2,
    +   2,   2,   2,   2,   3,   3,   3,   3,   3,   3,
    +   3,   3,   3,   4,   4,   4,   4,   4,   4,   4,
    +   4,   4,   4,   4,   4,   4,   4,   4,   4,   4,
    +   4,   4,   4,   4,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5,   5,   5,   5,   5,   5,   5,   5,   5,
    +   5,   5};
    int ierr, delta;
    unsigned int nbits;
    unsigned long metric, maxcycles, cycles;
    unsigned long npoints=readwavfile(argc, argv, &buffer);

    fftw_complex *fftin, *fftout;
    fftw_plan MYPLAN;
    int nfft1=2*1024*1024;
    int nfft2=nfft1/32;
    int nh2=nfft2/2;
    double df=12000.0/nfft1;
    int i0=1500.0/df+0.5;
    
//    printf("%d %d %d %d\n",i0, nfft1, nfft2, npoints);
    fftin=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*nfft1);
    fftout=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*nfft1);
    MYPLAN = fftw_plan_dft_1d(nfft1, fftin, fftout, FFTW_FORWARD, FFTW_ESTIMATE);

    for (i=0; i<npoints; i++) {
        fftin[i][0]=buffer[i];
        fftin[i][1]=0.0;
    }
    for (i=npoints; i<nfft1; i++) {
        fftin[i][0]=0.0;
        fftin[i][1]=0.0;
    }

    fftw_execute(MYPLAN);

    fftw_free(fftin);
    fftw_destroy_plan(MYPLAN);
    
    fftin=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*nfft2);
    for (i=0; i<nfft2; i++){
        j=i0+i;
        if( i>nh2 )
            j=j-nfft2;
        fftin[i][0]=fftout[j][0];
        fftin[i][1]=fftout[j][1];
    }
    
    fftw_free(fftout);
    fftout=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*nfft2);
    MYPLAN = fftw_plan_dft_1d(nfft2, fftin, fftout, FFTW_BACKWARD, FFTW_ESTIMATE);
    fftw_execute(MYPLAN);
    
//    npoints=nfft2;
    
    float dt=1.0/375.0;

    idat=malloc(sizeof(double)*nfft2);
    qdat=malloc(sizeof(double)*nfft2);

    for (i=0; i<nfft2; i++) {
        idat[i]=fftout[i][0]/1000.0;
        qdat[i]=fftout[i][1]/1000.0;
    }
    
    fftw_free(fftin);
    fftw_free(fftout);
    fftw_destroy_plan(MYPLAN);

    
    getStats(idat, qdat, nfft2, &mi, &mq, &mi2, &mq2, &miq);
    printf("total power: %4.1f dB\n",10*log10(mi2+mq2));

// Do ffts over 2 symbols, stepped by half symbols
    int nffts=4*floor((npoints/32.0)/512)-1;
    fftin=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*512);
    fftout=(fftw_complex*) fftw_malloc(sizeof(fftw_complex)*512);
    MYPLAN = fftw_plan_dft_1d(512, fftin, fftout, FFTW_FORWARD, FFTW_ESTIMATE);
    
    float ps[512][nffts];
    memset(ps,0.0, sizeof(float)*512*nffts);
    for (i=0; i<nffts; i++) {
        for(j=0; j<512; j++ ){
            k=i*128+j;
            fftin[j][0]=idat[k];
            fftin[j][1]=qdat[k];
        }
        fftw_execute(MYPLAN);
        for (j=0; j<512; j++ ){
            k=j+256;
            if( k>511 )
                k=k-512;
            ps[j][i]=fftout[k][0]*fftout[k][0]+fftout[k][1]*fftout[k][1];
        }
    }
    
    fftw_destroy_plan(MYPLAN);
    fftw_free(fftin);
    fftw_free(fftout);

    float psavg[512];
    memset(psavg,0.0, sizeof(float)*512);
    for (i=0; i<nffts; i++) {
        for (j=0; j<512; j++) {
            psavg[j]=psavg[j]+ps[j][i];
        }
    }
    
// smooth with 7-point window
    int window[7]={1,1,1,1,1,1,1};
    float smspec[411];
    for (i=0; i<411; i++) {
        smspec[i]=0.0;
        for(j=-3; j<=3; j++) {
            k=256-205+i+j;
            smspec[i]=smspec[i]+window[j+3]*psavg[k];
        }
    }

    float tmpsort[411];
    for (j=0; j<411; j++)
        tmpsort[j]=smspec[j];
    qsort(tmpsort, 411, sizeof(float), floatcomp);
    
    float noise_level = tmpsort[120];
    
    for (j=0; j<411; j++) {
        smspec[j]=smspec[j]/noise_level - 1.0;
        if( smspec[j] < pow(10.0,(-33+26.5)/10))
            smspec[j]=0.1;
            continue;
    }

// find all local maxima in smoothed spectrum.
    float freq0[200],snr0[200],drift0[200],shift0[200];
    int npk=0;
    df=375.0/256.0/2;
    for(j=1; j<410; j++) {
        if((smspec[j]>smspec[j-1]) & (smspec[j]>smspec[j+1])) {
            freq0[npk]=(j-205)*df;
            snr0[npk]=10*log10(smspec[j])-26.5;
            npk++;
        }
    }

// do course estimates freq, drift and shift using k1jt's basic approach, more or less.
    int idrift,ifr,if0,ifd,k0,kindex;
    float smax, pmax,ss,pow,p0,p1,p2,p3;
    for(j=0; j<npk; j++) {
        smax=-1e30;
        if0=freq0[j]/df+256;

// look for time offsets of up to +/- 4 seconds
// nominal start time is 2 seconds into the file

        for (ifr=if0-1; ifr<=if0+1; ifr++) {
        for( k0=-6; k0<18; k0++)
        {
            // drift model is linear, deviation of +/- drift/2 over the
            // span of 162 symbols.
            
            for (idrift=-14; idrift<=14; idrift++)
            {
                ss=0.0;
                pow=0.0;
                for (k=0; k<162; k++)
                {
                    ifd=ifr+((float)k-81.0)/81.0*idrift/(8*df);
                    kindex=k0+2*k;
                    if( kindex < nffts ) {
                    p0=ps[ifd-3][kindex];
                    p1=ps[ifd-1][kindex];
                    p2=ps[ifd+1][kindex];
                    p3=ps[ifd+3][kindex];
                    ss=ss+(2*pr3[k]-1)*(p3+p1-p0-p2);
                    pow=pow+p0+p1+p2+p3;
                    }
                }
                if( ss > smax ) {
                    smax=ss;
                    pmax=pow;
                    shift0[j]=128*(k0+1)*dt;
                    drift0[j]=idrift/4.0;
                    freq0[j]=(ifr-256)*df;
                }
//                printf("drift %d  k0 %d  sync %f\n",idrift,k0,ss/pow);
            }
        }
        }
//        printf("npk %d freq %.1f drift %.1f t0 %.1f sync %.2f\n",j,freq0[j],drift0[j],shift0[j],smax/pmax);
    }

/*
    FILE *spf=fopen("spec","w");
    for(j=0; j<512; j++) {
        for(k=0; k<nffts; k++) {
            fprintf(spf,"%f\n",ps[j][k]);
        }
    }
    fclose(spf);
*/

    nbits=81;
    symbols=malloc(sizeof(char)*nbits*2);
    
    float f1, fstep, sync, drift1;
    int shift1, lagmin, lagmax, lagstep;
    decdata=malloc((nbits+7)/8);
    grid=malloc(sizeof(char)*5);
    callsign=malloc(sizeof(char)*7);
    char allcalls[npk][7];
    memset(allcalls,0,sizeof(char)*npk*7);
    
    printf(" n    freq   drift  snr    sync     dt                 cycles\n");
    int uniques=0;
    
    for (j=0; j<npk; j++) {

// now refine the estimates of freq, shift using sync (0<sync<1) as a metric.
// assume that optimization over freq and shift can be done sequentially
// use function sync_and_demodulate - it has three modes of operation:
// mode is the last argument:
// 0 no frequency or drift search. find best time lag.
// 1 no time lag or drift search. find best frequency.
// 2 no frequency or time lag search. calculate soft-decision symbols using passed frequency and shift.
        f1=freq0[j];
        drift1=drift0[j];

// fine search for best sync lag (mode 0)
        fstep=0.0;
        lagmin=shift0[j]/dt-128;
        lagmax=shift0[j]/dt+128;
        lagstep=8;
        sync_and_demodulate(idat, qdat, npoints, symbols, &f1, fstep, &shift1, lagmin, lagmax, lagstep, &drift1, &sync, 0);
//        printf("after demodulate %f %d %f %f\n",f1,shift1,drift1,sync);

// fine search for frequency peak (mode 1)
        fstep=0.1;
        sync_and_demodulate(idat, qdat, npoints, symbols, &f1, fstep, &shift1, lagmin, lagmax, lagstep, &drift1, &sync, 1);
//        printf("after demodulate %f %d %f %f\n",f1,shift1,drift1,sync);

// use mode 2 to get soft-decision symbols
        sync_and_demodulate(idat, qdat, npoints, symbols, &f1, fstep, &shift1, lagmin, lagmax, lagstep, &drift1, &sync, 2);
        
        deinterleave(symbols);

// for now, we just use a fixed metric table rather than generating one based
// on snr.
//        amp=100;
//        mebn0=5.0;
//        mesn0=mebn0+10*log10(0.5);
//        mnoise=sqrt(0.5/pow(10.,mesn0/10.));
    
//    ierr = gen_met( mettab, amp, mnoise, 0.5, 10);
//    printf("after gen_met %d\n",ierr);
//    for (i=0; i<256; i++) {
//        printf("%d %d %d\n",i,mettab[0][i],mettab[1][i]);
//    }

        delta=50;
        maxcycles=10000;

/*        printf("\n");
        for (i=0; i<162; i=i+9)
        {
            for (int m=0; m<9;m++) {
                printf("%4d ",symbols[i+m]);
            }
            printf("\n");
        }
 */
        ierr= fano(&metric,&cycles,decdata,symbols,nbits,mettab,delta,maxcycles);

//        printf("ierr %d metric %d  cycles %d\n",ierr,metric,cycles/81);

        if( !ierr )
        {
            for(i=0; i<11; i++) {
                if( decdata[i]>128 )
                {
                    message[i]=decdata[i]-256;
                } else {
                    message[i]=decdata[i];
                }
            }
            unpack50(message,&n1,&n2);
            unpackcall(n1,callsign);
            unpackgrid(n2, grid);
            int ntype = (n2&127) - 64;

// de-dupe using callsign
            int dupe=0;
            for (i=0; i<npk; i++) {
                if( !strcmp(callsign,allcalls[i]) )
                    dupe=1;
            }
            if( !dupe) {
                uniques++;
                strcpy(allcalls[uniques],callsign);
            printf("%.3d %6.1f %6.1f %6.1f   %4.2f %6.1f  %s %s %d %lu\n",j,f1,drift1,snr0[j],
                   sync,shift1*dt-2.0, callsign, grid, ntype, cycles/81);
            }
        } else {
        }
    }
	return 0;
}