// أداة اختبار المفكّك المدمج rin_imgdec.h: تفكّ ملفاً وتطبع "W H" ثم بايتات RGBA الخام إلى stdout.
// تُقارَن مع PIL بواسطة tests/terminal/imgdec_compare.py.
// البناء:  g++ -std=c++17 -O2 -Icli/linux/src tests/terminal/imgdec_dump.cpp -o /tmp/dump -lz
#include "terminal/rin_imgdec.h"
#include <cstdio>
int main(int, char** argv){ rin::imgdec::Image im; std::string err; bool un=false;
  if(!rin::imgdec::decodeFile(argv[1],im,err,&un)){ fprintf(stderr,"ERR %s\n",err.c_str()); return 3; }
  printf("%d %d\n",im.w,im.h); fwrite(im.rgba.data(),1,im.rgba.size(),stdout); }
