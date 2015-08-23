#include "mendex.h"

#include <stdarg.h>

#include <kpathsea/tex-file.h>
#include <unicode/unorm2.h>

#include "exkana.h"
#include "exvar.h"

int line_length=0;

static void printpage(struct index *ind, FILE *fp, int num, char *lbuff);
static int range_check(struct index ind, int count, char *lbuff);
static void linecheck(char *lbuff, char *tmpbuff);
static void crcheck(char *lbuff, FILE *fp);
static void index_normalize(UChar *istr, UChar *ini);
static int initial_cmp_char(UChar *ini, UChar ch);
static const UNormalizer2* unormalizer_NFD;

#define M_NONE      0
#define M_TO_UPPER  1
#define M_TO_TITLE  2
#define M_TO_LOWER  -1

#define CHOSEONG_KIYEOK       0x1100
#define CHOSEONG_TIKEUT_RIEUL 0x115E

/* All buffers have size BUFFERLEN.  */
#define BUFFERLEN 4096
#define INITIALLEN 4

#ifdef HAVE___VA_ARGS__
/* Use C99 variadic macros if they are supported.  */
/* We would like to use sizeof(buf) instead of BUFFERLEN but that fails
   for, e.g., gcc-4.8.3 on Cygwin and gcc-4.5.3 on NetBSD.  */
#define SPRINTF(buf, ...) \
    snprintf(buf, BUFFERLEN, __VA_ARGS__)
#define SAPPENDF(buf, ...) \
    snprintf(buf + strlen(buf), BUFFERLEN - strlen(buf), __VA_ARGS__)
#else
/* Alternatively use static inline functions.  */
static inline int SPRINTF(char *buf, const char *format, ...)
{
    va_list argptr;
    int n;

    va_start(argptr, format);
    n = vsnprintf(buf, BUFFERLEN, format, argptr);
    va_end(argptr);

    return n;
}
static inline int SAPPENDF(char *buf, const char *format, ...)
{
    va_list argptr;
    int n;

    va_start(argptr, format);
    n = vsnprintf(buf + strlen(buf), BUFFERLEN - strlen(buf), format, argptr);
    va_end(argptr);

    return n;
}
#endif

static void fprint_uchar(FILE *fp, const UChar *a, const int mode, const int len)
{
	int k;
	char str[15], *ret;
	UChar istr[5], jstr[5];
	int olen, wclen;
	UErrorCode perr;

	if (len<0) {
		for (k=0; a[k] || k<4; k++) istr[k]=a[k];
		wclen=k;
	} else {
		wclen = (U16_IS_LEAD(a[0]) && U16_IS_TRAIL(a[1])) ? 2 : 1;
			      istr[0]=a[0];
		if (wclen==2) istr[1]=a[1];
	}
	istr[wclen]=L'\0';
	if (mode==M_TO_UPPER) {
		u_strcpy(jstr,istr);
		perr = U_ZERO_ERROR;
		u_strToUpper(istr,5,jstr,wclen,"",&perr);
	} else if (mode==M_TO_LOWER) {
		u_strcpy(jstr,istr);
		perr = U_ZERO_ERROR;
		u_strToLower(istr,5,jstr,wclen,"",&perr);
	} else if (mode==M_TO_TITLE) {
		u_strcpy(jstr,istr);
		perr = U_ZERO_ERROR;
		u_strToTitle(istr,5,jstr,wclen,NULL,"",&perr);
	}
	perr = U_ZERO_ERROR;
	ret = u_strToUTF8(str, 15, &olen, istr, wclen, &perr);
	fprintf(fp,"%s",str);
}

#ifdef WIN32
/*   fprintf with convert kanji code   */
int fprintf2(FILE *fp, const char *format, ...)
{
    char print_buff[8000];
    va_list argptr;
    int n;

    va_start(argptr, format);
    n = vsnprintf(print_buff, sizeof print_buff, format, argptr);
    va_end(argptr);

    fputs(print_buff, fp);
    return n;
}
#endif

void warn_printf(FILE *fp, const char *format, ...)
{
    char print_buff[8000];
    va_list argptr;

    va_start(argptr, format);
    vsnprintf(print_buff, sizeof print_buff, format, argptr);
    va_end(argptr);

    warn++;    
    fputs(print_buff, stderr);
    if (fp!=stderr) fputs(print_buff, fp);
}

void verb_printf(FILE *fp, const char *format, ...)
{
    char print_buff[8000];
    va_list argptr;

    va_start(argptr, format);
    vsnprintf(print_buff, sizeof print_buff, format, argptr);
    va_end(argptr);

    if (verb!=0)    fputs(print_buff, stderr);
    if (fp!=stderr) fputs(print_buff, fp);
}


/*   write ind file   */
void indwrite(char *filename, struct index *ind, int pagenum)
{
	int i,j,hpoint=0,tpoint=0;
	char lbuff[BUFFERLEN],obuff[BUFFERLEN];
	UChar datama[256],initial[INITIALLEN],initial_prev[INITIALLEN];
	FILE *fp;
	int conv_euc_to_euc;
	UErrorCode perr;

	if (filename && kpse_out_name_ok(filename)) fp=fopen(filename,"wb");
	else {
		fp=stdout;
#ifdef WIN32
		setmode(fileno(fp), _O_BINARY);
#endif
	}

	convert(atama,datama);
	fputs(preamble,fp);

	if (fpage>0) {
		fprintf(fp,"%s%d%s",setpage_prefix,pagenum,setpage_suffix);
	}
	perr=U_ZERO_ERROR;
	unormalizer_NFD=unorm2_getInstance(NULL, "nfc", UNORM2_DECOMPOSE, &perr);

	for (i=line_length=0;i<lines;i++) {
		index_normalize(ind[i].dic[0], initial);
		if (i==0) {
			if (is_latin(initial)||is_cyrillic(initial)||is_greek(initial)) {
				if (lethead_flag!=0) {
					fputs(lethead_prefix,fp);
					fprint_uchar(fp,initial,lethead_flag,-1);
					fputs(lethead_suffix,fp);
				}
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[0]);
				SPRINTF(lbuff,"%s%s",item_0,obuff);
			}
			else if (is_jpn_kana(initial)) {
				if (lethead_flag!=0) {
					fputs(lethead_prefix,fp);
					for (j=hpoint;j<(u_strlen(datama));j++) {
						if (initial_cmp_char(initial,datama[j])) {
							fprint_uchar(fp,&atama[j-1],M_NONE,1);
							hpoint=j;
							break;
						}
					}
					if (j==(u_strlen(datama))) {
						fprint_uchar(fp,&atama[j-1],M_NONE,1);
					}
					fputs(lethead_suffix,fp);
				}
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[0]);
				SPRINTF(lbuff,"%s%s",item_0,obuff);
				for (hpoint=0;hpoint<(u_strlen(datama));hpoint++) {
					if (initial_cmp_char(initial,datama[hpoint])) {
						break;
					}
				}
			}
			else if (is_kor_hngl(initial)) {
				if (lethead_flag!=0) {
					fputs(lethead_prefix,fp);
					for (j=tpoint;j<(u_strlen(tumunja));j++) {
						if (initial_cmp_char(initial,tumunja[j])) {
							fprint_uchar(fp,&tumunja[j-1],M_NONE,1);
							tpoint=j;
							break;
						}
					}
					if (j==(u_strlen(tumunja))) {
						fprint_uchar(fp,&tumunja[j-1],M_NONE,1);
					}
					fputs(lethead_suffix,fp);
				}
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[0]);
				SPRINTF(lbuff,"%s%s",item_0,obuff);
				for (tpoint=0;tpoint<(u_strlen(tumunja));tpoint++) {
					if (initial_cmp_char(initial,tumunja[tpoint])) {
						break;
					}
				}
			}
			else {
				if (lethead_flag!=0) {
					if (symbol_flag && strlen(symbol)) {
						fprintf(fp,"%s%s%s",lethead_prefix,symbol,lethead_suffix);
					}
					else if (lethead_flag>0) {
						fprintf(fp,"%s%s%s",lethead_prefix,symhead_positive,lethead_suffix);
					}
					else if (lethead_flag<0) {
						fprintf(fp,"%s%s%s",lethead_prefix,symhead_negative,lethead_suffix);
					}
				}
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[0]);
				SPRINTF(lbuff,"%s%s",item_0,obuff);
			}
			switch (ind[i].words) {
			case 1:
				SAPPENDF(lbuff,"%s",delim_0);
				break;

			case 2:
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[1]);
				SAPPENDF(lbuff,"%s%s",item_x1,obuff);
				SAPPENDF(lbuff,"%s",delim_1);
				break;

			case 3:
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[1]);
				SAPPENDF(lbuff,"%s%s",item_x1,obuff);
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[2]);
				SAPPENDF(lbuff,"%s%s",item_x2,obuff);
				SAPPENDF(lbuff,"%s",delim_2);
				break;

			default:
				break;
			}
			printpage(ind,fp,i,lbuff);
		}
		else {
			index_normalize(ind[i-1].dic[0], initial_prev);
			if (is_latin(initial)||is_cyrillic(initial)||is_greek(initial)) {
				if (ss_comp(initial,initial_prev)) {
					fputs(group_skip,fp);
					if (lethead_flag!=0) {
						fputs(lethead_prefix,fp);
						fprint_uchar(fp,initial,lethead_flag,-1);
						fputs(lethead_suffix,fp);
					}
				}
			}
			else if (is_jpn_kana(initial)) {
				for (j=hpoint;j<(u_strlen(datama));j++) {
					if (initial_cmp_char(initial,datama[j])) {
						break;
					}
				}
				if ((j!=hpoint)||(j==0)) {
					hpoint=j;
					fputs(group_skip,fp);
					if (lethead_flag!=0) {
						fputs(lethead_prefix,fp);
						fprint_uchar(fp,&atama[j-1],M_NONE,1);
						fputs(lethead_suffix,fp);
					}
				}
				else if (!initial_cmp_char(initial,KATA_N) && ss_comp(initial_prev,initial)) {
					fputs(group_skip,fp);
					if (lethead_flag!=0) {
						fputs(lethead_prefix,fp);
						fprint_uchar(fp,initial,M_NONE,1);
						fputs(lethead_suffix,fp);
					}
				}
			}
			else if (is_kor_hngl(initial)) {
				for (j=tpoint;j<(u_strlen(tumunja));j++) {
					if (initial_cmp_char(initial,tumunja[j])) {
						break;
					}
				}
				if ((j!=tpoint)||(j==0)) {
					tpoint=j;
					fputs(group_skip,fp);
					if (lethead_flag!=0) {
						fputs(lethead_prefix,fp);
						fprint_uchar(fp,&tumunja[j-1],M_NONE,1);
						fputs(lethead_suffix,fp);
					}
				}
				else if (!initial_cmp_char(initial,CHOSEONG_TIKEUT_RIEUL) && ss_comp(initial_prev,initial)) {
					fputs(group_skip,fp);
					if (lethead_flag!=0) {
						fputs(lethead_prefix,fp);
						fprint_uchar(fp,&tumunja[j-1],M_NONE,1);
						fputs(lethead_suffix,fp);
					}
				}
			}
			else {
				if ((is_latin(initial_prev))||is_cyrillic(initial_prev)
					||is_greek(initial_prev)||(is_jpn_kana(initial_prev)
					||is_kor_hngl(initial_prev))){
					fputs(group_skip,fp);
					if (lethead_flag!=0 && symbol_flag) {
						if (strlen(symbol)) {
							fprintf(fp,"%s%s%s",lethead_prefix,symbol,lethead_suffix);
						}
						else if (lethead_flag>0) {
							fprintf(fp,"%s%s%s",lethead_prefix,symhead_positive,lethead_suffix);
						}
						else if (lethead_flag<0) {
							fprintf(fp,"%s%s%s",lethead_prefix,symhead_negative,lethead_suffix);
						}
					}
				}
			}

			switch (ind[i].words) {
			case 1:
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[0]);
				SAPPENDF(lbuff,"%s%s%s",item_0,obuff,delim_0);
				break;

			case 2:
				if (u_strcmp(ind[i-1].idx[0],ind[i].idx[0])!=0 || u_strcmp(ind[i-1].dic[0],ind[i].dic[0])!=0) {
					widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[0]);
					SAPPENDF(lbuff,"%s%s%s",item_0,obuff,item_x1);
				}
				else {
					if (ind[i-1].words==1) {
						SAPPENDF(lbuff,"%s",item_01);
					}
					else {
						SAPPENDF(lbuff,"%s",item_1);
					}
				}
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[1]);
				SAPPENDF(lbuff,"%s",obuff);
				SAPPENDF(lbuff,"%s",delim_1);
				break;

			case 3:
				if (u_strcmp(ind[i-1].idx[0],ind[i].idx[0])!=0 || u_strcmp(ind[i-1].dic[0],ind[i].dic[0])!=0) {
					widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[0]);
					SAPPENDF(lbuff,"%s%s",item_0,obuff);
					widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[1]);
					SAPPENDF(lbuff,"%s%s%s",item_x1,obuff,item_x2);
				}
				else if (ind[i-1].words==1) {
					widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[1]);
					SAPPENDF(lbuff,"%s%s%s",item_01,obuff,item_x2);
				}
				else if (u_strcmp(ind[i-1].idx[1],ind[i].idx[1])!=0 || u_strcmp(ind[i-1].dic[1],ind[i].dic[1])!=0) {
					widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[1]);
					if (ind[i-1].words==2) SAPPENDF(lbuff,"%s%s%s",item_1,obuff,item_12);
					else SAPPENDF(lbuff,"%s%s%s",item_1,obuff,item_x2);
				}
				else {
					SAPPENDF(lbuff,"%s",item_2);
				}
				widechar_to_multibyte(obuff,BUFFERLEN,ind[i].idx[2]);
				SAPPENDF(lbuff,"%s%s",obuff,delim_2);
				break;

			default:
				break;
			}
			printpage(ind,fp,i,lbuff);
		}
	}
	fputs(postamble,fp);

	if (filename) fclose(fp);
}

/*   write page block   */
static void printpage(struct index *ind, FILE *fp, int num, char *lbuff)
{
	int i,j,k,cc;
	char buff[BUFFERLEN],tmpbuff[BUFFERLEN],errbuff[BUFFERLEN],obuff[BUFFERLEN];

	buff[0]=tmpbuff[0]='\0';

	crcheck(lbuff,fp);
	line_length=strlen(lbuff);

	for(j=0;j<ind[num].num;j++) {
		cc=range_check(ind[num],j,lbuff);
		if (cc>j) {
			if (pnumconv(ind[num].p[j].page,ind[num].p[j].attr[0])==pnumconv(ind[num].p[cc].page,ind[num].p[cc].attr[0])) {
				j=cc-1;
				continue;
			}
/* range process */
			if (ind[num].p[j].enc[0]==range_open
				|| ind[num].p[j].enc[0]==range_close)
				ind[num].p[j].enc++;
			if (strlen(ind[num].p[j].enc)>0) {
				SPRINTF(buff,"%s%s%s",encap_prefix,ind[num].p[j].enc,encap_infix);
			}
			if (strlen(suffix_3p)>0 && (pnumconv(ind[num].p[cc].page,ind[num].p[cc].attr[0])-pnumconv(ind[num].p[j].page,ind[num].p[j].attr[0]))==2) {
				SAPPENDF(buff,"%s%s",ind[num].p[j].page,suffix_3p);
			}
			else if (strlen(suffix_mp)>0 && (pnumconv(ind[num].p[cc].page,ind[num].p[cc].attr[0])-pnumconv(ind[num].p[j].page,ind[num].p[j].attr[0]))>=2) {
				SAPPENDF(buff,"%s%s",ind[num].p[j].page,suffix_mp);
			}
			else if (strlen(suffix_2p)>0 && (pnumconv(ind[num].p[cc].page,ind[num].p[cc].attr[0])-pnumconv(ind[num].p[j].page,ind[num].p[j].attr[0]))==1) {
				SAPPENDF(buff,"%s%s",ind[num].p[j].page,suffix_2p);
			}
			else {
				SAPPENDF(buff,"%s%s",ind[num].p[j].page,delim_r);
				SAPPENDF(buff,"%s",ind[num].p[cc].page);
			}
			SAPPENDF(tmpbuff,"%s",buff);
			buff[0]='\0';
			if (strlen(ind[num].p[j].enc)>0) {
				SAPPENDF(tmpbuff,"%s",encap_suffix);
			}
			linecheck(lbuff,tmpbuff);
			j=cc;
			if (j==ind[num].num) {
				goto PRINT;
			}
			else {
				SAPPENDF(tmpbuff,"%s",delim_n);
				linecheck(lbuff,tmpbuff);
			}
		}
		else if (strlen(ind[num].p[j].enc)>0) {
/* normal encap */
			if (ind[num].p[j].enc[0]==range_close) {
				SPRINTF(errbuff,"Warning: Unmatched range closing operator \'%c\',",range_close);
				for (i=0;i<ind[num].words;i++) {
					widechar_to_multibyte(obuff,BUFFERLEN,ind[num].idx[i]);
					SAPPENDF(errbuff,"%s.",obuff);
				}
				warn_printf(efp, "%s\n", errbuff);
				ind[num].p[j].enc++;
			}
			if (strlen(ind[num].p[j].enc)>0) {
				SAPPENDF(tmpbuff,"%s%s%s",encap_prefix,ind[num].p[j].enc,encap_infix);
				SAPPENDF(tmpbuff,"%s%s%s",ind[num].p[j].page,encap_suffix,delim_n);
				linecheck(lbuff,tmpbuff);
			}
			else {
				SAPPENDF(tmpbuff,"%s%s",ind[num].p[j].page,delim_n);
				linecheck(lbuff,tmpbuff);
			}
		}
		else {
/* no encap */
			SAPPENDF(tmpbuff,"%s%s",ind[num].p[j].page,delim_n);
			linecheck(lbuff,tmpbuff);
		}
	}

	if (ind[num].p[j].enc[0]==range_open) {
		SPRINTF(errbuff,"Warning: Unmatched range opening operator \'%c\',",range_open);
		for (k=0;k<ind[num].words;k++) {
			widechar_to_multibyte(obuff,BUFFERLEN,ind[num].idx[k]);
			SAPPENDF(errbuff,"%s.",obuff);
		}
		warn_printf(efp, "%s\n", errbuff);
		ind[num].p[j].enc++;
	}
	else if (ind[num].p[j].enc[0]==range_close) {
		SPRINTF(errbuff,"Warning: Unmatched range closing operator \'%c\',",range_close);
		for (k=0;k<ind[num].words;k++) {
			widechar_to_multibyte(obuff,BUFFERLEN,ind[num].idx[k]);
			SAPPENDF(errbuff,"%s.",obuff);
		}
		warn_printf(efp, "%s\n", errbuff);
		ind[num].p[j].enc++;
	}
	if (strlen(ind[num].p[j].enc)>0) {
		SAPPENDF(tmpbuff,"%s%s%s",encap_prefix,ind[num].p[j].enc,encap_infix);
		SAPPENDF(tmpbuff,"%s%s",ind[num].p[j].page,encap_suffix);
	}
	else {
		SAPPENDF(tmpbuff,"%s",ind[num].p[j].page);
	}
	linecheck(lbuff,tmpbuff);

PRINT:
	fputs(lbuff,fp);
	fputs(delim_t,fp);
	lbuff[0]='\0';
}

static int range_check(struct index ind, int count, char *lbuff)
{
	int i,j,k,cc1,cc2,start,force=0;
	char tmpbuff[BUFFERLEN],errbuff[BUFFERLEN],obuff[BUFFERLEN];

	for (i=count;i<ind.num+1;i++) {
		if (ind.p[i].enc[0]==range_close) {
			SPRINTF(errbuff,"Warning: Unmatched range closing operator \'%c\',",range_close);
			widechar_to_multibyte(obuff,BUFFERLEN,ind.idx[0]);
			SAPPENDF(errbuff,"%s.",obuff);
			warn_printf(efp, "%s\n", errbuff);
			ind.p[i].enc++;
		}
		if (ind.p[i].enc[0]==range_open) {
			start=i;
			ind.p[i].enc++;
			for (j=i;j<ind.num+1;j++) {
				if (strcmp(ind.p[start].enc,ind.p[j].enc)) {
					if (ind.p[j].enc[0]==range_close) {
						ind.p[j].enc++;
						ind.p[j].enc[0]='\0';
						force=1;
						break;
					}
					else if (j!=i && ind.p[j].enc[0]==range_open) {
						SPRINTF(errbuff,"Warning: Unmatched range opening operator \'%c\',",range_open);
						for (k=0;k<ind.words;k++) {
							widechar_to_multibyte(obuff,BUFFERLEN,ind.idx[k]);
							SAPPENDF(errbuff,"%s.",obuff);
						}
						warn_printf(efp, "%s\n", errbuff);
						ind.p[j].enc++;
					}
					if (strlen(ind.p[j].enc)>0) {
						SPRINTF(tmpbuff,"%s%s%s",encap_prefix,ind.p[j].enc,encap_infix);
						SAPPENDF(tmpbuff,"%s%s%s",ind.p[j].page,encap_suffix,delim_n);
						linecheck(lbuff,tmpbuff);
					}
				}
			}
			if (j==ind.num+1) {
					SPRINTF(errbuff,"Warning: Unmatched range opening operator \'%c\',",range_open);
					for (k=0;k<ind.words;k++) {
						widechar_to_multibyte(obuff,BUFFERLEN,ind.idx[k]);
						SAPPENDF(errbuff,"%s.",obuff);
					}
					warn_printf(efp, "%s\n", errbuff);
			}
			i=j-1;
		}
		else if (prange && i<ind.num) {
			if (chkcontinue(ind.p,i)
				&& (!strcmp(ind.p[i].enc,ind.p[i+1].enc)
				|| ind.p[i+1].enc[0]==range_open))
				continue;
			else {
				i++;
				break;
			}
		}
		else {
			i++;
			break;
		}
	}
	cc1=pnumconv(ind.p[i-1].page,ind.p[i-1].attr[0]);
	cc2=pnumconv(ind.p[count].page,ind.p[count].attr[0]);
	if (cc1>=cc2+2 || (cc1>=cc2+1 && strlen(suffix_2p)) || force) {
		return i-1;
	}
	else return count;
}

/*   check line length   */
static void linecheck(char *lbuff, char *tmpbuff)
{
	if (line_length+strlen(tmpbuff)>line_max) {
		SAPPENDF(lbuff,"\n%s%s",indent_space,tmpbuff);
		line_length=indent_length+strlen(tmpbuff);
		tmpbuff[0]='\0';
	}
	else {
		SAPPENDF(lbuff,"%s",tmpbuff);
		line_length+=strlen(tmpbuff);
		tmpbuff[0]='\0';
	}
}

static void crcheck(char *lbuff, FILE *fp)
{
	int i;
	char buff[BUFFERLEN];

	for (i=strlen(lbuff);i>=0;i--) {
		if (lbuff[i]=='\n') {
			strncpy(buff,lbuff,i+1);
			buff[i+1]='\0';
			fputs(buff,fp);
			strcpy(buff,&lbuff[i+1]);
			strcpy(lbuff,buff);
			break;
		}
	}
}

static void index_normalize(UChar *istr, UChar *ini)
{
	int k;
	UChar ch,src[2],dest[8],strX[4],strY[4],strZ[4];
	UErrorCode perr;
	UCollationResult order;

	ch=istr[0];
	ini[1]=L'\0';

	if (is_hiragana(ch)) {
		ch+=KATATOP-HIRATOP; /* hiragana -> katakana */
	}
	if (is_katakana(ch)) {
		for (k=0;k<=KATAEND-KATATOP;k++) {
			if (ch==k+KATATOP) {
				ini[0]=kanatable[k];
				return;
			}
		}
		/* error */
		ini[0]=ch;
		return;
	}
	else if (ch==0x309F) { ini[0]=0x30E8; return; } /* HIRAGANA YORI */
	else if (ch==0x30FF) { ini[0]=0x30B3; return; }  /* KATAKANA KOTO */
	else if (is_kor_hngl(&ch)) {
		if ((ch>=0xAC00)&&(ch<=0xD7AF)) {               /* Hangul Syllables */
			ch=(ch-0xAC00)/(21*28)+CHOSEONG_KIYEOK; /* convert to Hangul Jamo, Initial consonants */
		}
		else switch (ch) {
			case 0x3131: case 0xFFA1:
			case 0x3200: case 0x320E: case 0x3260: case 0x326E:
				ch=0x1100; break; /* ᄀ */
			case 0x3132: case 0xFFA2:
				ch=0x1101; break; /* ᄁ */
			case 0x3134: case 0xFFA4:
			case 0x3201: case 0x320F: case 0x3261: case 0x326F:
				ch=0x1102; break; /* ᄂ */
			case 0x3137: case 0xFFA7:
			case 0x3202: case 0x3210: case 0x3262: case 0x3270:
				ch=0x1103; break; /* ᄃ */
			case 0x3138: case 0xFFA8:
				ch=0x1104; break; /* ᄄ */
			case 0x3139: case 0xFFA9:
			case 0x3203: case 0x3211: case 0x3263: case 0x3271:
				ch=0x1105; break; /* ᄅ */
			case 0x3141: case 0xFFB1:
			case 0x3204: case 0x3212: case 0x3264: case 0x3272:
				ch=0x1106; break; /* ᄆ */
			case 0x3142: case 0xFFB2:
			case 0x3205: case 0x3213: case 0x3265: case 0x3273:
				ch=0x1107; break; /* ᄇ */
			case 0x3143: case 0xFFB3:
				ch=0x1108; break; /* ᄈ */
			case 0x3145: case 0xFFB5:
			case 0x3206: case 0x3214: case 0x3266: case 0x3274:
				ch=0x1109; break; /* ᄉ */
			case 0x3146: case 0xFFB6:
				ch=0x110A; break; /* ᄊ */
			case 0x3147: case 0xFFB7:
			case 0x3207: case 0x3215: case 0x3267: case 0x3275:
			case 0x321D: case 0x321E: case 0x327E: /* ㈝ ㈞ ㉾ */
				ch=0x110B; break; /* ᄋ */
			case 0x3148: case 0xFFB8:
			case 0x3208: case 0x3216: case 0x3268: case 0x3276:
			case 0x321C: case 0x327D:              /* ㈜ ㉽ */
				ch=0x110C; break; /* ᄌ */
			case 0x3149: case 0xFFB9:
				ch=0x110D; break; /* ᄍ */
			case 0x314A: case 0xFFBA:
			case 0x3209: case 0x3217: case 0x3269: case 0x3277:
			case 0x327C:                           /* ㉼ */
				ch=0x110E; break; /* ᄎ */
			case 0x314B: case 0xFFBB:
			case 0x320A: case 0x3218: case 0x326A: case 0x3278:
				ch=0x110F; break; /* ᄏ */
			case 0x314C: case 0xFFBC:
			case 0x320B: case 0x3219: case 0x326B: case 0x3279:
				ch=0x1110; break; /* ᄐ */
			case 0x314D: case 0xFFBD:
			case 0x320C: case 0x321A: case 0x326C: case 0x327A:
				ch=0x1111; break; /* ᄑ */
			case 0x314E: case 0xFFBE:
			case 0x320D: case 0x321B: case 0x326D: case 0x327B:
				ch=0x1112; break; /* ᄒ */
		}
		ini[0]=ch;
		return;
	}
	else if (ch==0x0C6||ch==0x0E6||ch==0x152||ch==0x153||ch==0x132||ch==0x133
		||ch==0x0DF||ch==0x1E9E||ch==0x13F||ch==0x140||ch==0x490||ch==0x491) {
		strX[0] = u_toupper(ch);  strX[1] = 0x00; /* ex. "Æ" "Œ" */
		switch (ch) {
			case 0x0C6: case 0x0E6:        /* Æ æ */
				strZ[0] = 0x41; break; /* A   */
			case 0x152: case 0x153:        /* Œ œ */
				strZ[0] = 0x4F; break; /* O   */
			case 0x0DF: case 0x1E9E:       /* ß ẞ */
				strZ[0] = 0x53; break; /* S   */
			case 0x132: case 0x133:        /* Ĳ ĳ */
				strZ[0] = 0x59;        /* Y   */
				strZ[1] = 0x00;
				if (ucol_equal(icu_collator, strZ, -1, strX, -1)) { ini[0]=0x59; return; }
				strZ[0] = 0x49; break; /* I   */
			case 0x13F: case 0x140:        /* Ŀ ŀ */
				strZ[0] = 0x4C; break; /* L   */
			case 0x490: case 0x491:        /* Ґ ґ */
				strZ[0] = 0x413; break; /* Г   */
		}
		strZ[1] = (ch==0x490||ch==0x491) ? 0x42F : 0x5A;
		strZ[2] = 0x00;                           /* ex. "AZ" "OZ" "ГЯ" */
		order = ucol_strcoll(icu_collator, strZ, -1, strX, -1);
		if (order==UCOL_GREATER) { ini[0]=strZ[0]; return; }  /* not ligature */
	}
	else if ((is_latin(&ch)&&ch>0x7F)||
		 (is_cyrillic(&ch)&&(ch<0x410||ch==0x419||ch==0x439||ch>0x44F))||
		 (is_greek(&ch)&&(ch<0x391||(ch>0x3A9&&ch<0x3B1)||ch>0x3C9))) {  /* check diacritic */
		src[0]=ch;  src[1]=0x00;
		perr=U_ZERO_ERROR;
		unorm2_normalize(unormalizer_NFD, src, 1, dest, 8, &perr);
		if (U_SUCCESS(perr)) {
			if      (is_latin(&ch))    { strZ[1] = 0x05A; }  /* Z */
			else if (is_cyrillic(&ch)) { strZ[1] = 0x42F; }  /* Я */
			else                       { strZ[1] = 0x3A9; }  /* Ω */
			strZ[0] = u_toupper(dest[0]);  strZ[2] = 0x00;   /* ex. "AZ" */
			strX[0] = u_toupper(ch);       strX[1] = 0x00;   /* ex. "Å"  */
			order = ucol_strcoll(icu_collator, strZ, -1, strX, -1);
			if (order==UCOL_LESS) { ini[0]=strX[0]; return; }  /* with diacritic */
			ch=dest[0];                                        /* without diacritic */
		}
	}
	if (is_latin(istr)&&u_strlen(istr)>1) {
		for(k=0; k<(u_strlen(istr)>2 ? 3 : 2); k++) {
			strX[k]=u_toupper(istr[k]);
		}
		strX[k]=L'\0';
		/* DZ, SZ or DZS for Hungarian, ad-hoc treatment */
		if ((strX[0]==0x44 || strX[0]==0x53) && strX[1]==0x5A) {         /* DZ SZ */
			strY[0]=0x44; strY[1]=0x5A; strY[2]=0x53; strY[3]=L'\0'; /* DZS */
			strZ[0]=0x44; strZ[1]=0x5A; strZ[2]=0x5A; strZ[3]=L'\0'; /* DZZ */
			order = ucol_strcoll(icu_collator, strZ, -1, strY, -1);
			if (order==UCOL_LESS) {
				ini[0]=strX[0]; ini[1]=strX[1];
				if (strX[0]==0x44 && strX[2]==0x53) { /* DZS */
					ini[2]=0x53; ini[3]=L'\0';
				} else {                              /* DZ SZ */
					ini[2]=L'\0';
				}
				return;
			}
		}
		/* DZ, DŽ for Slovak or Serbo-Croatian, ad-hoc treatment */
		if (strX[0]==0x44 && (strX[1]==0x5A || strX[1]==0x17D)) {        /* DZ DŽ */
			strY[0]=0x44; strY[1]=0x17D; strY[2]=L'\0';              /* DŽ  */
			strZ[0]=0x44; strZ[1]=0x5A; strZ[2]=0x5A; strZ[3]=L'\0'; /* DZZ */
			order = ucol_strcoll(icu_collator, strZ, -1, strY, -1);
			if (order==UCOL_LESS) {
				if (strX[1]==0x5A) {
					strY[0]=0xD4; strY[1]=L'\0';                /* Ô  */
					strZ[0]=0x4F; strZ[1]=0x5A; strZ[2]=L'\0';  /* OZ */
					order = ucol_strcoll(icu_collator, strZ, -1, strY, -1);
					if (order==UCOL_LESS) {  /* Slovak DZ */
						ini[0]=strX[0]; ini[1]=strX[1];
						ini[2]=L'\0';
						return;
					}
				} else {
					ini[0]=strX[0]; ini[1]=strX[1]; /* DŽ */
					ini[2]=L'\0';
					return;
				}
			}
		}
		/* other digraphs */
		if ((strX[0]==0x43 && strX[1]==0x48) ||                   /* CH */
		    (strX[0]==0x4C && strX[1]==0x4C) ||                   /* LL */
		   ((strX[0]==0x4C || strX[0]==0x4E) && strX[1]==0x4A) || /* LJ NJ */
		   ((strX[0]==0x43 || strX[0]==0x5A) && strX[1]==0x53) || /* CS ZS */
		   ((strX[0]==0x47 || strX[0]==0x4C || strX[0]==0x4E || strX[0]==0x54) && strX[1]==0x59)) /* GY LY NY TY */
		{
			strX[2]=L'\0';
			strZ[0]=strX[0]; strZ[1]=0x5A; strZ[2]=L'\0';
			order = ucol_strcoll(icu_collator, strZ, -1, strX, -1);
			if (order==UCOL_LESS) {
				ini[0]=strX[0]; ini[1]=strX[1]; ini[2]=L'\0';
				return;
			}
		}
	}
	ini[0]=u_toupper(ch);
	return;
}

static int initial_cmp_char(UChar *ini, UChar ch)
{
	UChar initial_tmp[INITIALLEN],istr[2];
	istr[0]=ch;
	istr[1]=L'\0';

	index_normalize(istr, initial_tmp);
	return (ss_comp(ini, initial_tmp)<0);
}
