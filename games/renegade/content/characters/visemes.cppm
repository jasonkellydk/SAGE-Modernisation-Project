// Ported from EA Combat/viseme.cpp, revision 3e00c3a1b97381bb28be89a35b856375e0629a08.
// Copyright 2025 Electronic Arts Inc. GPL-3.0-or-later.
export module games.renegade.content.characters.visemes;
import std;
namespace renegade::content::VisemeDetail {
enum {VISEME_CAT,VISEME_EAT,VISEME_IF,VISEME_OX,VISEME_WET,VISEME_UP,VISEME_ROAR,VISEME_FAVE,VISEME_CHURCH,VISEME_BUMP,VISEME_THOUGH,VISEME_TOLD,VISEME_CAGE,VISEME_NEW,VISEME_SIZE,VISEME_EARTH};
struct VisemeTableItem {std::string_view LetterCombination;int Visemes[2];};
inline bool IsVowel(char c) {return c && (c=='a'||c=='e'||c=='i'||c=='o'||c=='u');}
inline bool IsConsonant(char c) {return c && !IsVowel(c);}
constexpr VisemeTableItem Table[] =
{
	{"air",		VISEME_IF, -1},
	{"ar",		VISEME_OX, -1},
	{"ch",		VISEME_CHURCH, -1},
	{"ea",		VISEME_EAT, -1},
	{"ee",		VISEME_EAT, -1},
	{"er",		VISEME_EARTH, -1},
	{"oa",		VISEME_OX, -1},
	{"oo",		VISEME_WET, -1},
	{"ou",		VISEME_OX, VISEME_WET},
	{"ow",		VISEME_WET},
	{"qu",		VISEME_WET, -1},
	{"sch",		VISEME_SIZE,	VISEME_CAGE},
	{"sh",		VISEME_CHURCH, -1},
	{"tch",		VISEME_CHURCH, -1},
	{"th",		VISEME_THOUGH, -1},
	{"gn",		VISEME_NEW, -1},
	{"kn",		VISEME_NEW, -1},
	{"eye",		VISEME_IF, -1},
	{"uy",		VISEME_IF, -1},
	{"ar",		VISEME_OX, -1},
	{"kw",		VISEME_WET, -1},
	{"and",		VISEME_NEW, VISEME_TOLD},
	{"ze",		VISEME_ROAR, VISEME_EAT},		
	{"ro",		VISEME_WET, VISEME_OX},
	{"one",		VISEME_WET, VISEME_CAT},		
	{"two",		VISEME_EAT, VISEME_WET},
	{"thr",		VISEME_THOUGH, VISEME_ROAR},
	{"fou",		VISEME_FAVE, VISEME_OX},
	{"ive",		VISEME_IF, VISEME_FAVE},
	{"six",		VISEME_UP, VISEME_IF},
	{"se",		VISEME_UP, VISEME_EAT},
	{"ven",		VISEME_FAVE, VISEME_EAT},
	{"eight",	VISEME_CAT, VISEME_EAT},
	{"ni",		VISEME_THOUGH, VISEME_IF},
	{"ne",		VISEME_THOUGH, -1},				
	
};

int Do_Letter_a(const char *pchar, char prevchar, int *viseme)
{
	int offset = 1;
	viseme[0] = VISEME_TOLD;
	viseme[1] = -1;
	if ( IsVowel(pchar[1]) )	{
		offset++;
		switch (pchar[1])	{
		case 'i':
			viseme[1] = VISEME_EAT;
			if ( pchar[2] == 's' ) {
				viseme[0] = VISEME_CAT;
			}
			break;
		case 'o':
			viseme[1] = VISEME_EAT;
			break;
		case 'u':
			switch (prevchar)	{
			case 'g':
				viseme[1] = VISEME_EAT;
				break;
			case 'l':
				viseme[0] = VISEME_CAT;
				break;
			case 's':
				viseme[0] = VISEME_CAT;
				viseme[1] = VISEME_WET;
				break;
			default:
				viseme[0] = VISEME_CAT;
			}
			break;
		}
	}
	else	{
		if ( IsVowel(pchar[2]) ) {
			viseme[1] = VISEME_EAT;
		}
		else {
			viseme[0] = VISEME_CAT;
		}
	}
	return(offset);
}
int Do_Letter_e(const char *pchar, char prevchar, int *viseme)
{
	int offset = 1;
	viseme[0] = VISEME_EAT;
	viseme[1] = -1;
	if ( IsVowel(pchar[1]) ) {
		offset++;
		switch ( pchar[1] ) {
		case 'a':
			if ( pchar[2] == 'u' ) {
				viseme[0] = VISEME_EAT;
				viseme[1] = VISEME_WET;
				offset++;
			}
			break;
		case 'i':
			if ( prevchar == 'h' ) {
				viseme[0] = VISEME_CAT;
				viseme[1] = VISEME_EAT;
			}
			else if ( pchar[2] == 'g' ) {
				viseme[0] = VISEME_TOLD;
				viseme[1] = VISEME_EAT;
			}
			else if ( pchar[2] == 'z' ) {
				viseme[0] = VISEME_EAT;
			}
			else {
				viseme[0] = VISEME_IF;
			}
			break;
		case 'o':
			if ( prevchar == 'p' ) {
				viseme[0] = VISEME_EAT;
			}
			else {
				viseme[0] = VISEME_TOLD;
			}
			break;
		case 'u':
			viseme[0] = VISEME_WET;
			break;
		}
	}
	else {
		if ( prevchar == 'b' || pchar[1] == 0 ) {
			viseme[0] = VISEME_EAT;
		}
		else {
			viseme[0] = VISEME_WET;
		}
	}
	return(offset);
}
int Do_Letter_i(const char *pchar, char prevchar, int *viseme)
{
	int offset = 1;
	viseme[0] = VISEME_IF;
	viseme[1] = -1;
	if ( IsVowel(pchar[1]) ) {
		offset++;
		viseme[0] = VISEME_TOLD;
		viseme[1] = -1;
		switch ( pchar[1] ) {
		case 'e':
			if ( pchar[2] == 0 && prevchar == 'l' ) {
				viseme[0] = VISEME_CAT;
				viseme[1] = VISEME_EAT;
			}
			else if ( pchar[2] == 'u' || pchar[2] == 'w' ) {
				viseme[0] = VISEME_WET;
				offset++;
			}
			else {
				viseme[0] = VISEME_EAT;
			}
			break;
		case 'o':
			viseme[0] = VISEME_EAT;
			break;
		}
	}
	else {
		viseme[0] = VISEME_IF;
		viseme[1] = -1;
	}
	return(offset);
}
int Do_Letter_o(const char *pchar, char , int *viseme)
{
	int offset = 1;
	viseme[0] = VISEME_OX;
	viseme[1] = -1;
	if ( IsVowel(pchar[1]) ) {
		offset++;
		viseme[0] = VISEME_UP;
		viseme[1] = -1;
		switch ( pchar[1] ) {
		case 'a':
			if ( pchar[-2] == 'b' && pchar[-1] == 'r' ) {
				viseme[0] = VISEME_UP;
			}
			else {
				viseme[0] = VISEME_OX;
				viseme[1] = VISEME_WET;
			}
			break;
		case 'e':
			viseme[0] = VISEME_WET;
			if ( pchar[-1] == 'd' && pchar[2] == 's' ) {
				viseme[0] = VISEME_UP;
			}
			else if ( pchar[-2] == 'p' && pchar[-1] == 'h' ) {
				viseme[0] = VISEME_EAT;
			}
			break;
		case 'i':
			viseme[0] = VISEME_UP;
			viseme[1] = VISEME_EAT;
			break;
		case 'o':
			viseme[0] = VISEME_WET;
			break;
		case 'u':
			viseme[0] = VISEME_WET;
			if ( pchar[-2] == 't' && pchar[-1] == 'r' ) {
				viseme[0] = VISEME_UP;
			}
			else if ( pchar[-2] == 't' && pchar[-1] == 'h' ) {
				if ( std::strncmp(&pchar[2], "ght", 3) == 0 ) {
					viseme[0] = VISEME_UP;
					offset += 3;
				}
			}
			else if ( pchar[2] == 'r' ) {
				viseme[0] = VISEME_TOLD;
				offset++;
			}
			break;
		}
	}
	else if ( pchar[1] ) {
		if ( IsVowel(pchar[2]) ) {
			viseme[0] = VISEME_WET;
			viseme[1] = -1;
		}
		else {
			viseme[0] = VISEME_OX;
			viseme[1] = -1;
		}
	}
	return(offset);
}
int Do_Letter_s(const char *pchar, char , int *viseme)
{
	int offset = 1;
	viseme[0] = VISEME_SIZE;
	viseme[1] = -1;
	if ( pchar[1] == 'h' || pchar[1] == 'u' )	{
		viseme[0] = VISEME_CHURCH;
	}
	else if ( pchar[1] == 'c' ) {
		if ( pchar[2] == 'h' ) {
			viseme[0] = VISEME_CHURCH;
			viseme[1] = VISEME_TOLD;
			offset = 3;
		}
		else {
			viseme[0] = VISEME_TOLD;
			offset = 2;
		}
	}
	else if ( std::strncmp(&pchar[1], "eou", 3) == 0 ) {
		viseme[0] = VISEME_CHURCH;
		offset = 4;
	}
	return(offset);
}
int Do_Letter_t(const char *pchar, char , int *viseme)
{
	int offset = 1;
	viseme[0] = VISEME_TOLD;
	if ( pchar[1] == 'c' ) {
		if ( pchar[2] == 'h' ) {
			viseme[0] = VISEME_CHURCH;
			offset = 3;
		}
	}
	else if ( pchar[1] == 'h' ) {
		viseme[0] = VISEME_THOUGH;
		offset = 2;
	}
	return(offset);
}
int Do_Letter_u(const char *pchar, char , int *viseme)
{
	int offset = 1;
	viseme[0] = VISEME_UP;
	if ( IsVowel(pchar[1]) ) {
		offset++;
		switch ( pchar[1] ) {
		case 'e':
		case 'i':
			viseme[0] = VISEME_WET;
			viseme[1] = -1;
			break;
		}
	}
	else {
		if ( IsConsonant(pchar[2]) )	{
			viseme[0] = VISEME_UP;
			viseme[1] = -1;
		}
	}
	return(offset);
}
}
export namespace renegade::content {
inline std::vector<std::uint32_t> SpeechVisemes(std::string_view text) {
    using namespace VisemeDetail;
    // The source uses a 128-byte phrase buffer. Padding also bounds its
    // two-character lookbehind and multi-character lookahead at both ends.
    std::array<char,136> buffer{};auto* phrase=buffer.data()+2;
    const auto size=std::min(text.size(),std::size_t{127});
    for(std::size_t i=0;i<size;++i) phrase[i]=text[i]>='A'&&text[i]<='Z' ? char(text[i]+32) : text[i];
    std::vector<std::uint32_t> result;int last=-1;char previous{};
    for(std::size_t index=0;index<size;) {
        const auto* current=phrase+index;std::array<int,2> poses{VISEME_TOLD,-1};std::size_t advance{};std::string_view best;
        for(const auto& entry:Table) if(std::string_view(current).starts_with(entry.LetterCombination) && entry.LetterCombination>best) {
            best=entry.LetterCombination;advance=best.size();std::copy_n(entry.Visemes,2,poses.begin());
        }
        if(!advance) {
            advance=1;
            switch(*current) {
            case 'a':advance=Do_Letter_a(current,previous,poses.data());break;
            case 'e':advance=Do_Letter_e(current,previous,poses.data());break;
            case 'i':advance=Do_Letter_i(current,previous,poses.data());break;
            case 'o':advance=Do_Letter_o(current,previous,poses.data());break;
            case 'u':advance=Do_Letter_u(current,previous,poses.data());break;
            case 's':advance=Do_Letter_s(current,previous,poses.data());break;
            case 't':advance=Do_Letter_t(current,previous,poses.data());break;
            case 'b':case 'm':case 'p':poses[0]=VISEME_BUMP;break;
            case 'c':case 'g':case 'k':case 'q':poses[0]=VISEME_CAGE;break;
            case 'd':poses[0]=VISEME_TOLD;break;
            case 'f':case 'v':poses[0]=VISEME_FAVE;break;
            case 'j':poses[0]=VISEME_CHURCH;break;
            case 'l':poses[0]=VISEME_THOUGH;break;
            case 'n':poses[0]=VISEME_NEW;break;
            case 'r':poses[0]=VISEME_ROAR;break;
            case 'w':poses[0]=VISEME_WET;break;
            case 'x':poses={VISEME_CAGE,VISEME_SIZE};break;
            case 'z':poses[0]=VISEME_SIZE;break;
            default:break;
            }
        }
        for(int pose:poses) if(pose>=0 && pose!=last) {result.push_back(std::uint32_t(pose));last=pose;if(result.size()==255) return result;}
        index+=advance;previous=phrase[index-1];
    }
    return result;
}
}
