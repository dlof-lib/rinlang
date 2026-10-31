// اختبار مُلوِّن indsin (rin_indsin_highlight.h): عينة وسوم + عينة صياغة قديمة + 20000 مدخل عشوائي (fuzz)
// يتحقق من: مقاطع مرتّبة وغير متداخلة وضمن حدود السطر، وعدم التعليق/الانهيار.
// البناء:  g++ -std=c++20 -I app/src/main/cpp/editor -I app/src/main/cpp tests/indsin_highlight/highlight_test.cpp -o /tmp/hl_test && /tmp/hl_test
#include "rin_indsin_highlight.h"
#include <cstdio>
#include <random>
using namespace rinedit;
static const char* nm(int k){ switch(k){case 36:return "BRK";case 37:return "TAG";case 38:return "CUS";case 39:return "ID";case 40:return "ATT";case 41:return "EVT";case 42:return "TOK";case 43:return "CLR";case 44:return "LEN";case 45:return "STA";case 46:return "BRC";case 47:return "STR";
 case 1:return "kw";case 2:return "str";case 3:return "num";case 5:return "op";case 7:return "at";case 9:return "call";case 10:return "cmt";case 20:return "const";case 21:return "fdef";case 22:return "ctl";case 26:return "prop";case 30:return "cmp";case 31:return "pun";case 32:return "flow";case 33:case 34:case 35:return "(r)";default:return "?";}}
static std::vector<std::string> split(const std::string&s){std::vector<std::string>v;std::string c;for(char ch:s){if(ch=='\n'){v.push_back(c);c.clear();}else c+=ch;}v.push_back(c);return v;}
static bool check(const std::vector<std::string>&L,const std::vector<HighlightSpan>&sp){
  int pl=-1,pe=0;for(auto&s:sp){ if(s.line<0||s.line>=(int)L.size()||s.startCol<0||s.endCol<=s.startCol||s.endCol>(int)L[s.line].size())return false;
    if(s.line<pl)return false; if(s.line==pl&&s.startCol<pe)return false; pl=s.line;pe=s.endCol;} return true;}
static void dump(const std::string& src){
  auto L=split(src); auto sp=computeIndsinHighlights(L);
  printf("ok=%d tags=%d\n",(int)check(L,sp),(int)looksLikeIndsinTags(L));
  size_t si=0;
  for(int li=0;li<(int)L.size();++li){ printf("%2d| %s\n",li+1,L[li].c_str()); std::string row;
    for(;si<sp.size()&&sp[si].line==li;++si){ printf("      [%s] %s\n",nm((int)sp[si].kind),L[li].substr(sp[si].startCol,sp[si].endCol-sp[si].startCol).c_str()); } }
}
int main(){
  dump(R"(<!-- عداد -->
<warp count={0} dark={false} />
<Screen#app size=phone bg=#0B1020>
  <Column#root gap=16 padding=spacious>
    <Text#value size=heading color="#fff">العدد: {count + 1}</Text>
    <Button#inc variant=gradient width=120px onTap={count = count + 1}>Add</Button>
    <Banner dismissible visible={count > 3 and !dark} />
    <Text text="Count = <<count>> &lt;ok&gt;" />
    <If cond={count == 2}><Text>hi <<user.name>></Text></If>
  </Column>
</Screen>
<Rin> fun report(n) { print "items: " + n; if n < 3 { return 1 } } </Rin>
<<each people as p, i>>
  <Card#row><Text><<p["name"]>> (<<p.age>>)</Text></Card>
<</each>>
)");
  printf("----- legacy\n");
  dump(R"(warp count = 0;
@view.Column=root gap=16;
  @view.Text=title;
    text="Hello" + str(count); size="heading"; color=#3498db;
    onTap=bump(1);
  .end/view
.end/view
)");
  // fuzz
  std::mt19937 rng(7); const char alpha[]="<>/{}()[]\"'#=!&|?:;,.@-_ \n\tabcWarpColumn123onTapx%*+\xd8\xa7";
  int bad=0;
  for(int it=0;it<20000;++it){ std::string s; int len=rng()%200; for(int k=0;k<len;++k)s+=alpha[rng()%(sizeof(alpha)-1)];
    auto L=split(s); auto sp=computeIndsinHighlights(L); if(!check(L,sp)){ if(bad++<3) printf("BAD: %s\n",s.c_str()); } }
  printf("fuzz bad=%d\n",bad);
}
