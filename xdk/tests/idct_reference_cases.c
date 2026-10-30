typedef void (*fn)(uint8_t*,int16_t*,int);
static unsigned rs=12345;static unsigned rnd(void){rs=rs*1664525u+1013904223u;return rs;}
static int test_kernels(void){
 fn c[4]={ff_h264_idct_add_reference,ff_h264_idct_dc_add_reference,ff_h264_idct8_add_reference,ff_h264_idct8_dc_add_reference};
 fn v[4]={idct_add_vmx,idct_dc_add_vmx,idct8_add_vmx,idct8_dc_add_vmx};
 const char *names[4]={"4x4 AC","4x4 DC","8x8 AC","8x8 DC"};
 int mode,f,t,i,failures=0;
 for(mode=0;mode<3;mode++)for(f=0;f<4;f++){
  int bad=0,count=f<2?16:64;
  for(t=0;t<2000;t++){
   uint8_t a[64*20],b[64*20],vb[128];int16_t cb[64];
   for(i=0;i<(int)sizeof(a);i++)a[i]=(uint8_t)(rnd()>>16);memcpy(b,a,sizeof(a));
   for(i=0;i<64;i++){
    int val=(int)((rnd()>>8)&1023)-512;
    if(mode)val=(int)((rnd()>>8)&4095)-2048;
    if(mode==2)val=(int16_t)(rnd()>>8);
    if(f&1)val=i?0:(int16_t)(rnd()>>8);
    cb[i]=(int16_t)val;vb[2*i]=(uint8_t)(val>>8);vb[2*i+1]=(uint8_t)val;
   }
   c[f](a+(t&3)*4,cb,64);v[f](b+(t&3)*4,(int16_t*)vb,64);
   if(memcmp(a,b,sizeof(a)))bad++;
   else for(i=0;i<count;i++)if(cb[i]!=(int16_t)((vb[2*i]<<8)|vb[2*i+1])){bad++;break;}
  }
  printf("%s range %s: %d/2000 mismatches\n",names[f],mode==2?"signed16":mode?"2048":"512",bad);failures+=bad;
 }
 return failures?1:0;
}

static int test_batches(void){
 int mode,op,t,i,j,failures=0;int offsets[48]={0};
 for(i=0;i<16;i++)offsets[i]=((i&1)*4+((i>>2)&1)*8)+(((i>>1)&1)*4+((i>>3)&1)*8)*64;
 for(i=0;i<4;i++)offsets[16+i]=offsets[32+i]=(i&1)*4+(i>>1)*4*64;
 for(mode=0;mode<3;mode++)for(op=0;op<4;op++){
  int bad=0;
  for(t=0;t<500;t++){
   uint8_t a[64*48],b[64*48],vb[48*32],nnz[120]={0};int16_t cb[48*16]={0};
   uint8_t *pa[2]={a+20*64,a+32*64},*pb[2]={b+20*64,b+32*64};
   for(i=0;i<(int)sizeof(a);i++)a[i]=(uint8_t)(rnd()>>16);memcpy(b,a,sizeof(a));
   for(i=0;i<36;i++){
    int kind,count;
    if(op==2 ? !(i>=16&&i<20||i>=32&&i<36) : i>=16)continue;
    if(op==3&&(i&3))continue;
    count=op==3?64:16;kind=(i+t)%3;
    if(kind==1){cb[i*16]=(int16_t)(rnd()>>8);nnz[xbox_scan8[i]]=1;}
    else if(kind==2){
     for(j=0;j<count;j++)cb[i*16+j]=mode==2?(int16_t)(rnd()>>8):(int16_t)((int)((rnd()>>8)&(mode?4095:1023))-(mode?2048:512));
     nnz[xbox_scan8[i]]=(uint8_t)count;
    }
   }
   for(i=0;i<48*16;i++){vb[2*i]=(uint8_t)(cb[i]>>8);vb[2*i+1]=(uint8_t)cb[i];}
   if(op==0){ff_h264_idct_add16_reference(a,offsets,cb,64,nnz);idct_add16_vmx(b,offsets,(int16_t*)vb,64,nnz);}
   else if(op==1){ff_h264_idct_add16intra_reference(a,offsets,cb,64,nnz);idct_add16intra_vmx(b,offsets,(int16_t*)vb,64,nnz);}
   else if(op==2){ff_h264_idct_add8_reference(pa,offsets,cb,64,nnz);idct_add8_vmx(pb,offsets,(int16_t*)vb,64,nnz);}
   else {ff_h264_idct8_add4_reference(a,offsets,cb,64,nnz);idct8_add4_vmx(b,offsets,(int16_t*)vb,64,nnz);}
   if(memcmp(a,b,sizeof(a)))bad++;
   else for(i=0;i<48*16;i++)if(cb[i]!=(int16_t)((vb[2*i]<<8)|vb[2*i+1])){bad++;break;}
  }
  printf("batch %d range %d: %d/500 mismatches\n",op,mode,bad);failures+=bad;
 }
 return failures?1:0;
}
int main(void){int a=test_kernels();int b=test_batches();return a||b;}
