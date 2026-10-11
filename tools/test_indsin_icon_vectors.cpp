// tools/test_indsin_icon_vectors.cpp — اختبار جدول الأيقونات المتجهية.  g++ -std=c++17 -Iapp/src/main/cpp/indsin tools/test_indsin_icon_vectors.cpp -o t && ./t
#include "rin_indsin_icons.h"
#include <iostream>
#include <set>
static int fails=0;
#define CHECK(c,l) do{ if(c) std::cout<<"  [PASS] "<<l<<"\n"; else {std::cout<<"  [FAIL] "<<l<<"\n"; fails++;} }while(0)
int main(){
  auto& r = indsin::iconRegistry();
  CHECK(r.has("home") && r.resolve("home")=="\u2302", "legacy glyph registry untouched");
  CHECK(!r.has("totally-not-a-real-icon"), "unknown name still absent");
  CHECK(indsin::kIconVecCount==187, "187 vector icons");
  CHECK(r.hasVector("settings") && r.hasVector("gear"), "alias gear -> settings");
  CHECK(r.canonicalName("delete")=="trash", "delete -> trash");
  CHECK(r.vectorPath("home").rfind("M3 11L12 4",0)==0, "home path content");
  CHECK(r.vectorPath("nope").empty(), "unknown vector -> empty");
  CHECK(r.svg("check",16,"#16a34a",2).find("stroke=\"#16a34a\"")!=std::string::npos, "svg() builds markup");
  r.registerVector("my-logo","M4 4H20V20H4Z"); CHECK(r.hasVector("my-logo"), "registerVector extends at runtime");
  // كل أسماء سجل المحرّك الـ27 + إضافات الويب لها مسار متجه
  const char* engine[]={"home","menu","close","search","settings","user","edit","delete","download","upload","play","pause","stop","check","plus","minus","arrow-left","arrow-right","arrow-up","arrow-down","eye","eye-off","lock","unlock","refresh","share","more"};
  int miss=0; for(auto n:engine) if(!r.hasVector(n)){miss++; std::cout<<"    missing "<<n<<"\n";}
  CHECK(miss==0, "all 27 engine icon names have vectors");
  const char* web[]={"star","heart","info","warning","error","bell","mail","trash","calendar","camera","phone","link","folder","file","back","next","up","down"};
  miss=0; for(auto n:web) if(!r.hasVector(n)){miss++; std::cout<<"    missing "<<n<<"\n";}
  CHECK(miss==0, "all web-preview extras have vectors");
  std::set<std::string> seen; bool dup=false; for(size_t i=0;i<indsin::kIconVecCount;++i) if(!seen.insert(indsin::kIconVecs[i].name).second) dup=true;
  CHECK(!dup, "no duplicate names");
  std::cout<<(fails?"FAILED":"ALL PASSED")<<"\n"; return fails;
}
