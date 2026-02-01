#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <string.h>
#include <stdio.h>

#ifndef BADDR
#define BADDR(bptr) ((APTR)((ULONG)(bptr) << 2))
#endif

static void PrintSegList(BPTR seglist)
{
    ULONG idx = 0;
    BPTR seg = seglist;

    while (seg) {
        ULONG *p = (ULONG*)BADDR(seg);
        ULONG sizeLongs = p[0];
        BPTR next = (BPTR)p[1];
        APTR base = (APTR)(p + 2);

        printf("seg %ld: base=%08lx size=%ld\n",
               idx,
               (ULONG)base,
               sizeLongs * 4UL);

        seg = next;
        idx++;
    }
}

static STRPTR BuildArgString(int argc, char **argv)
{
    ULONG total = 2;
    STRPTR s;
    int i;

    for (i = 2; i < argc; i++)
        total += (ULONG)strlen(argv[i]) + 1;

    s = (STRPTR)AllocVec(total, MEMF_PUBLIC | MEMF_CLEAR);
    if (!s) return NULL;

    {
        ULONG pos = 0;
        for (i = 2; i < argc; i++) {
            ULONG len = (ULONG)strlen(argv[i]);
            memcpy(s + pos, argv[i], len);
            pos += len;
            s[pos++] = ' ';
        }
        s[pos++] = '\n';
        s[pos] = 0;
    }

    return s;
}

int main(int argc, char **argv)
{
    BPTR seglist;
    STRPTR argstr;
    LONG rc;

    if (argc < 2) {
        PutStr("usage: hunk_run <exe> [args]\n");
        return 20;
    }

    seglist = LoadSeg(argv[1]);

    if (!seglist) {
        PutStr("LoadSeg failed\n");
        return 20;
    }

    PrintSegList(seglist);

    argstr = BuildArgString(argc, argv);

    rc = RunCommand(seglist, 16384,
                    argstr ? argstr : (STRPTR)"\n",
                    argstr ? (LONG)strlen(argstr) : 1);

    if (argstr) FreeVec(argstr);

    UnLoadSeg(seglist);
    return rc;
}
