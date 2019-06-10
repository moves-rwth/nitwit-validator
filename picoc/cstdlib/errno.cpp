/* string.h library for large systems - small embedded systems use clibrary.c instead */
#include <errno.h>
#include "../interpreter.hpp"

#ifndef BUILTIN_MINI_STDLIB

#ifdef EACCES
static int EACCESValue = EACCES;
#endif

#ifdef EADDRINUSE
static int EADDRINUSEValue = EADDRINUSE;
#endif

#ifdef EADDRNOTAVAIL
static int EADDRNOTAVAILValue = EADDRNOTAVAIL;
#endif

#ifdef EAFNOSUPPORT
static int EAFNOSUPPORTValue = EAFNOSUPPORT;
#endif

#ifdef EAGAIN
static int EAGAINValue = EAGAIN;
#endif

#ifdef EALREADY
static int EALREADYValue = EALREADY;
#endif

#ifdef EBADF
static int EBADFValue = EBADF;
#endif

#ifdef EBADMSG
static int EBADMSGValue = EBADMSG;
#endif

#ifdef EBUSY
static int EBUSYValue = EBUSY;
#endif

#ifdef ECANCELED
static int ECANCELEDValue = ECANCELED;
#endif

#ifdef ECHILD
static int ECHILDValue = ECHILD;
#endif

#ifdef ECONNABORTED
static int ECONNABORTEDValue = ECONNABORTED;
#endif

#ifdef ECONNREFUSED
static int ECONNREFUSEDValue = ECONNREFUSED;
#endif

#ifdef ECONNRESET
static int ECONNRESETValue = ECONNRESET;
#endif

#ifdef EDEADLK
static int EDEADLKValue = EDEADLK;
#endif

#ifdef EDESTADDRREQ
static int EDESTADDRREQValue = EDESTADDRREQ;
#endif

#ifdef EDOM
static int EDOMValue = EDOM;
#endif

#ifdef EDQUOT
static int EDQUOTValue = EDQUOT;
#endif

#ifdef EEXIST
static int EEXISTValue = EEXIST;
#endif

#ifdef EFAULT
static int EFAULTValue = EFAULT;
#endif

#ifdef EFBIG
static int EFBIGValue = EFBIG;
#endif

#ifdef EHOSTUNREACH
static int EHOSTUNREACHValue = EHOSTUNREACH;
#endif

#ifdef EIDRM
static int EIDRMValue = EIDRM;
#endif

#ifdef EILSEQ
static int EILSEQValue = EILSEQ;
#endif

#ifdef EINPROGRESS
static int EINPROGRESSValue = EINPROGRESS;
#endif

#ifdef EINTR
static int EINTRValue = EINTR;
#endif

#ifdef EINVAL
static int EINVALValue = EINVAL;
#endif

#ifdef EIO
static int EIOValue = EIO;
#endif

#ifdef EISCONN
static int EISCONNValue = EISCONN;
#endif

#ifdef EISDIR
static int EISDIRValue = EISDIR;
#endif

#ifdef ELOOP
static int ELOOPValue = ELOOP;
#endif

#ifdef EMFILE
static int EMFILEValue = EMFILE;
#endif

#ifdef EMLINK
static int EMLINKValue = EMLINK;
#endif

#ifdef EMSGSIZE
static int EMSGSIZEValue = EMSGSIZE;
#endif

#ifdef EMULTIHOP
static int EMULTIHOPValue = EMULTIHOP;
#endif

#ifdef ENAMETOOLONG
static int ENAMETOOLONGValue = ENAMETOOLONG;
#endif

#ifdef ENETDOWN
static int ENETDOWNValue = ENETDOWN;
#endif

#ifdef ENETRESET
static int ENETRESETValue = ENETRESET;
#endif

#ifdef ENETUNREACH
static int ENETUNREACHValue = ENETUNREACH;
#endif

#ifdef ENFILE
static int ENFILEValue = ENFILE;
#endif

#ifdef ENOBUFS
static int ENOBUFSValue = ENOBUFS;
#endif

#ifdef ENODATA
static int ENODATAValue = ENODATA;
#endif

#ifdef ENODEV
static int ENODEVValue = ENODEV;
#endif

#ifdef ENOENT
static int ENOENTValue = ENOENT;
#endif

#ifdef ENOEXEC
static int ENOEXECValue = ENOEXEC;
#endif

#ifdef ENOLCK
static int ENOLCKValue = ENOLCK;
#endif

#ifdef ENOLINK
static int ENOLINKValue = ENOLINK;
#endif

#ifdef ENOMEM
static int ENOMEMValue = ENOMEM;
#endif

#ifdef ENOMSG
static int ENOMSGValue = ENOMSG;
#endif

#ifdef ENOPROTOOPT
static int ENOPROTOOPTValue = ENOPROTOOPT;
#endif

#ifdef ENOSPC
static int ENOSPCValue = ENOSPC;
#endif

#ifdef ENOSR
static int ENOSRValue = ENOSR;
#endif

#ifdef ENOSTR
static int ENOSTRValue = ENOSTR;
#endif

#ifdef ENOSYS
static int ENOSYSValue = ENOSYS;
#endif

#ifdef ENOTCONN
static int ENOTCONNValue = ENOTCONN;
#endif

#ifdef ENOTDIR
static int ENOTDIRValue = ENOTDIR;
#endif

#ifdef ENOTEMPTY
static int ENOTEMPTYValue = ENOTEMPTY;
#endif

#ifdef ENOTRECOVERABLE
static int ENOTRECOVERABLEValue = ENOTRECOVERABLE;
#endif

#ifdef ENOTSOCK
static int ENOTSOCKValue = ENOTSOCK;
#endif

#ifdef ENOTSUP
static int ENOTSUPValue = ENOTSUP;
#endif

#ifdef ENOTTY
static int ENOTTYValue = ENOTTY;
#endif

#ifdef ENXIO
static int ENXIOValue = ENXIO;
#endif

#ifdef EOPNOTSUPP
static int EOPNOTSUPPValue = EOPNOTSUPP;
#endif

#ifdef EOVERFLOW
static int EOVERFLOWValue = EOVERFLOW;
#endif

#ifdef EOWNERDEAD
static int EOWNERDEADValue = EOWNERDEAD;
#endif

#ifdef EPERM
static int EPERMValue = EPERM;
#endif

#ifdef EPIPE
static int EPIPEValue = EPIPE;
#endif

#ifdef EPROTO
static int EPROTOValue = EPROTO;
#endif

#ifdef EPROTONOSUPPORT
static int EPROTONOSUPPORTValue = EPROTONOSUPPORT;
#endif

#ifdef EPROTOTYPE
static int EPROTOTYPEValue = EPROTOTYPE;
#endif

#ifdef ERANGE
static int ERANGEValue = ERANGE;
#endif

#ifdef EROFS
static int EROFSValue = EROFS;
#endif

#ifdef ESPIPE
static int ESPIPEValue = ESPIPE;
#endif

#ifdef ESRCH
static int ESRCHValue = ESRCH;
#endif

#ifdef ESTALE
static int ESTALEValue = ESTALE;
#endif

#ifdef ETIME
static int ETIMEValue = ETIME;
#endif

#ifdef ETIMEDOUT
static int ETIMEDOUTValue = ETIMEDOUT;
#endif

#ifdef ETXTBSY
static int ETXTBSYValue = ETXTBSY;
#endif

#ifdef EWOULDBLOCK
static int EWOULDBLOCKValue = EWOULDBLOCK;
#endif

#ifdef EXDEV
static int EXDEVValue = EXDEV;
#endif


/* creates various system-dependent definitions */
void StdErrnoSetupFunc(Picoc *pc)
{
    /* defines */
#ifdef EACCES
    VariableDefinePlatformVar(pc, nullptr, "EACCES", &pc->IntType, (union AnyValue *)&EACCESValue, FALSE);
#endif

#ifdef EADDRINUSE
    VariableDefinePlatformVar(pc, nullptr, "EADDRINUSE", &pc->IntType, (union AnyValue *)&EADDRINUSEValue, FALSE);
#endif

#ifdef EADDRNOTAVAIL
    VariableDefinePlatformVar(pc, nullptr, "EADDRNOTAVAIL", &pc->IntType, (union AnyValue *)&EADDRNOTAVAILValue, FALSE);
#endif

#ifdef EAFNOSUPPORT
    VariableDefinePlatformVar(pc, nullptr, "EAFNOSUPPORT", &pc->IntType, (union AnyValue *)&EAFNOSUPPORTValue, FALSE);
#endif

#ifdef EAGAIN
    VariableDefinePlatformVar(pc, nullptr, "EAGAIN", &pc->IntType, (union AnyValue *)&EAGAINValue, FALSE);
#endif

#ifdef EALREADY
    VariableDefinePlatformVar(pc, nullptr, "EALREADY", &pc->IntType, (union AnyValue *)&EALREADYValue, FALSE);
#endif

#ifdef EBADF
    VariableDefinePlatformVar(pc, nullptr, "EBADF", &pc->IntType, (union AnyValue *)&EBADFValue, FALSE);
#endif

#ifdef EBADMSG
    VariableDefinePlatformVar(pc, nullptr, "EBADMSG", &pc->IntType, (union AnyValue *)&EBADMSGValue, FALSE);
#endif

#ifdef EBUSY
    VariableDefinePlatformVar(pc, nullptr, "EBUSY", &pc->IntType, (union AnyValue *)&EBUSYValue, FALSE);
#endif

#ifdef ECANCELED
    VariableDefinePlatformVar(pc, nullptr, "ECANCELED", &pc->IntType, (union AnyValue *)&ECANCELEDValue, FALSE);
#endif

#ifdef ECHILD
    VariableDefinePlatformVar(pc, nullptr, "ECHILD", &pc->IntType, (union AnyValue *)&ECHILDValue, FALSE);
#endif

#ifdef ECONNABORTED
    VariableDefinePlatformVar(pc, nullptr, "ECONNABORTED", &pc->IntType, (union AnyValue *)&ECONNABORTEDValue, FALSE);
#endif

#ifdef ECONNREFUSED
    VariableDefinePlatformVar(pc, nullptr, "ECONNREFUSED", &pc->IntType, (union AnyValue *)&ECONNREFUSEDValue, FALSE);
#endif

#ifdef ECONNRESET
    VariableDefinePlatformVar(pc, nullptr, "ECONNRESET", &pc->IntType, (union AnyValue *)&ECONNRESETValue, FALSE);
#endif

#ifdef EDEADLK
    VariableDefinePlatformVar(pc, nullptr, "EDEADLK", &pc->IntType, (union AnyValue *)&EDEADLKValue, FALSE);
#endif

#ifdef EDESTADDRREQ
    VariableDefinePlatformVar(pc, nullptr, "EDESTADDRREQ", &pc->IntType, (union AnyValue *)&EDESTADDRREQValue, FALSE);
#endif

#ifdef EDOM
    VariableDefinePlatformVar(pc, nullptr, "EDOM", &pc->IntType, (union AnyValue *)&EDOMValue, FALSE);
#endif

#ifdef EDQUOT
    VariableDefinePlatformVar(pc, nullptr, "EDQUOT", &pc->IntType, (union AnyValue *)&EDQUOTValue, FALSE);
#endif

#ifdef EEXIST
    VariableDefinePlatformVar(pc, nullptr, "EEXIST", &pc->IntType, (union AnyValue *)&EEXISTValue, FALSE);
#endif

#ifdef EFAULT
    VariableDefinePlatformVar(pc, nullptr, "EFAULT", &pc->IntType, (union AnyValue *)&EFAULTValue, FALSE);
#endif

#ifdef EFBIG
    VariableDefinePlatformVar(pc, nullptr, "EFBIG", &pc->IntType, (union AnyValue *)&EFBIGValue, FALSE);
#endif

#ifdef EHOSTUNREACH
    VariableDefinePlatformVar(pc, nullptr, "EHOSTUNREACH", &pc->IntType, (union AnyValue *)&EHOSTUNREACHValue, FALSE);
#endif

#ifdef EIDRM
    VariableDefinePlatformVar(pc, nullptr, "EIDRM", &pc->IntType, (union AnyValue *)&EIDRMValue, FALSE);
#endif

#ifdef EILSEQ
    VariableDefinePlatformVar(pc, nullptr, "EILSEQ", &pc->IntType, (union AnyValue *)&EILSEQValue, FALSE);
#endif

#ifdef EINPROGRESS
    VariableDefinePlatformVar(pc, nullptr, "EINPROGRESS", &pc->IntType, (union AnyValue *)&EINPROGRESSValue, FALSE);
#endif

#ifdef EINTR
    VariableDefinePlatformVar(pc, nullptr, "EINTR", &pc->IntType, (union AnyValue *)&EINTRValue, FALSE);
#endif

#ifdef EINVAL
    VariableDefinePlatformVar(pc, nullptr, "EINVAL", &pc->IntType, (union AnyValue *)&EINVALValue, FALSE);
#endif

#ifdef EIO
    VariableDefinePlatformVar(pc, nullptr, "EIO", &pc->IntType, (union AnyValue *)&EIOValue, FALSE);
#endif

#ifdef EISCONN
    VariableDefinePlatformVar(pc, nullptr, "EISCONN", &pc->IntType, (union AnyValue *)&EISCONNValue, FALSE);
#endif

#ifdef EISDIR
    VariableDefinePlatformVar(pc, nullptr, "EISDIR", &pc->IntType, (union AnyValue *)&EISDIRValue, FALSE);
#endif

#ifdef ELOOP
    VariableDefinePlatformVar(pc, nullptr, "ELOOP", &pc->IntType, (union AnyValue *)&ELOOPValue, FALSE);
#endif

#ifdef EMFILE
    VariableDefinePlatformVar(pc, nullptr, "EMFILE", &pc->IntType, (union AnyValue *)&EMFILEValue, FALSE);
#endif

#ifdef EMLINK
    VariableDefinePlatformVar(pc, nullptr, "EMLINK", &pc->IntType, (union AnyValue *)&EMLINKValue, FALSE);
#endif

#ifdef EMSGSIZE
    VariableDefinePlatformVar(pc, nullptr, "EMSGSIZE", &pc->IntType, (union AnyValue *)&EMSGSIZEValue, FALSE);
#endif

#ifdef EMULTIHOP
    VariableDefinePlatformVar(pc, nullptr, "EMULTIHOP", &pc->IntType, (union AnyValue *)&EMULTIHOPValue, FALSE);
#endif

#ifdef ENAMETOOLONG
    VariableDefinePlatformVar(pc, nullptr, "ENAMETOOLONG", &pc->IntType, (union AnyValue *)&ENAMETOOLONGValue, FALSE);
#endif

#ifdef ENETDOWN
    VariableDefinePlatformVar(pc, nullptr, "ENETDOWN", &pc->IntType, (union AnyValue *)&ENETDOWNValue, FALSE);
#endif

#ifdef ENETRESET
    VariableDefinePlatformVar(pc, nullptr, "ENETRESET", &pc->IntType, (union AnyValue *)&ENETRESETValue, FALSE);
#endif

#ifdef ENETUNREACH
    VariableDefinePlatformVar(pc, nullptr, "ENETUNREACH", &pc->IntType, (union AnyValue *)&ENETUNREACHValue, FALSE);
#endif

#ifdef ENFILE
    VariableDefinePlatformVar(pc, nullptr, "ENFILE", &pc->IntType, (union AnyValue *)&ENFILEValue, FALSE);
#endif

#ifdef ENOBUFS
    VariableDefinePlatformVar(pc, nullptr, "ENOBUFS", &pc->IntType, (union AnyValue *)&ENOBUFSValue, FALSE);
#endif

#ifdef ENODATA
    VariableDefinePlatformVar(pc, nullptr, "ENODATA", &pc->IntType, (union AnyValue *)&ENODATAValue, FALSE);
#endif

#ifdef ENODEV
    VariableDefinePlatformVar(pc, nullptr, "ENODEV", &pc->IntType, (union AnyValue *)&ENODEVValue, FALSE);
#endif

#ifdef ENOENT
    VariableDefinePlatformVar(pc, nullptr, "ENOENT", &pc->IntType, (union AnyValue *)&ENOENTValue, FALSE);
#endif

#ifdef ENOEXEC
    VariableDefinePlatformVar(pc, nullptr, "ENOEXEC", &pc->IntType, (union AnyValue *)&ENOEXECValue, FALSE);
#endif

#ifdef ENOLCK
    VariableDefinePlatformVar(pc, nullptr, "ENOLCK", &pc->IntType, (union AnyValue *)&ENOLCKValue, FALSE);
#endif

#ifdef ENOLINK
    VariableDefinePlatformVar(pc, nullptr, "ENOLINK", &pc->IntType, (union AnyValue *)&ENOLINKValue, FALSE);
#endif

#ifdef ENOMEM
    VariableDefinePlatformVar(pc, nullptr, "ENOMEM", &pc->IntType, (union AnyValue *)&ENOMEMValue, FALSE);
#endif

#ifdef ENOMSG
    VariableDefinePlatformVar(pc, nullptr, "ENOMSG", &pc->IntType, (union AnyValue *)&ENOMSGValue, FALSE);
#endif

#ifdef ENOPROTOOPT
    VariableDefinePlatformVar(pc, nullptr, "ENOPROTOOPT", &pc->IntType, (union AnyValue *)&ENOPROTOOPTValue, FALSE);
#endif

#ifdef ENOSPC
    VariableDefinePlatformVar(pc, nullptr, "ENOSPC", &pc->IntType, (union AnyValue *)&ENOSPCValue, FALSE);
#endif

#ifdef ENOSR
    VariableDefinePlatformVar(pc, nullptr, "ENOSR", &pc->IntType, (union AnyValue *)&ENOSRValue, FALSE);
#endif

#ifdef ENOSTR
    VariableDefinePlatformVar(pc, nullptr, "ENOSTR", &pc->IntType, (union AnyValue *)&ENOSTRValue, FALSE);
#endif

#ifdef ENOSYS
    VariableDefinePlatformVar(pc, nullptr, "ENOSYS", &pc->IntType, (union AnyValue *)&ENOSYSValue, FALSE);
#endif

#ifdef ENOTCONN
    VariableDefinePlatformVar(pc, nullptr, "ENOTCONN", &pc->IntType, (union AnyValue *)&ENOTCONNValue, FALSE);
#endif

#ifdef ENOTDIR
    VariableDefinePlatformVar(pc, nullptr, "ENOTDIR", &pc->IntType, (union AnyValue *)&ENOTDIRValue, FALSE);
#endif

#ifdef ENOTEMPTY
    VariableDefinePlatformVar(pc, nullptr, "ENOTEMPTY", &pc->IntType, (union AnyValue *)&ENOTEMPTYValue, FALSE);
#endif

#ifdef ENOTRECOVERABLE
    VariableDefinePlatformVar(pc, nullptr, "ENOTRECOVERABLE", &pc->IntType, (union AnyValue *)&ENOTRECOVERABLEValue, FALSE);
#endif

#ifdef ENOTSOCK
    VariableDefinePlatformVar(pc, nullptr, "ENOTSOCK", &pc->IntType, (union AnyValue *)&ENOTSOCKValue, FALSE);
#endif

#ifdef ENOTSUP
    VariableDefinePlatformVar(pc, nullptr, "ENOTSUP", &pc->IntType, (union AnyValue *)&ENOTSUPValue, FALSE);
#endif

#ifdef ENOTTY
    VariableDefinePlatformVar(pc, nullptr, "ENOTTY", &pc->IntType, (union AnyValue *)&ENOTTYValue, FALSE);
#endif

#ifdef ENXIO
    VariableDefinePlatformVar(pc, nullptr, "ENXIO", &pc->IntType, (union AnyValue *)&ENXIOValue, FALSE);
#endif

#ifdef EOPNOTSUPP
    VariableDefinePlatformVar(pc, nullptr, "EOPNOTSUPP", &pc->IntType, (union AnyValue *)&EOPNOTSUPPValue, FALSE);
#endif

#ifdef EOVERFLOW
    VariableDefinePlatformVar(pc, nullptr, "EOVERFLOW", &pc->IntType, (union AnyValue *)&EOVERFLOWValue, FALSE);
#endif

#ifdef EOWNERDEAD
    VariableDefinePlatformVar(pc, nullptr, "EOWNERDEAD", &pc->IntType, (union AnyValue *)&EOWNERDEADValue, FALSE);
#endif

#ifdef EPERM
    VariableDefinePlatformVar(pc, nullptr, "EPERM", &pc->IntType, (union AnyValue *)&EPERMValue, FALSE);
#endif

#ifdef EPIPE
    VariableDefinePlatformVar(pc, nullptr, "EPIPE", &pc->IntType, (union AnyValue *)&EPIPEValue, FALSE);
#endif

#ifdef EPROTO
    VariableDefinePlatformVar(pc, nullptr, "EPROTO", &pc->IntType, (union AnyValue *)&EPROTOValue, FALSE);
#endif

#ifdef EPROTONOSUPPORT
    VariableDefinePlatformVar(pc, nullptr, "EPROTONOSUPPORT", &pc->IntType, (union AnyValue *)&EPROTONOSUPPORTValue, FALSE);
#endif

#ifdef EPROTOTYPE
    VariableDefinePlatformVar(pc, nullptr, "EPROTOTYPE", &pc->IntType, (union AnyValue *)&EPROTOTYPEValue, FALSE);
#endif

#ifdef ERANGE
    VariableDefinePlatformVar(pc, nullptr, "ERANGE", &pc->IntType, (union AnyValue *)&ERANGEValue, FALSE);
#endif

#ifdef EROFS
    VariableDefinePlatformVar(pc, nullptr, "EROFS", &pc->IntType, (union AnyValue *)&EROFSValue, FALSE);
#endif

#ifdef ESPIPE
    VariableDefinePlatformVar(pc, nullptr, "ESPIPE", &pc->IntType, (union AnyValue *)&ESPIPEValue, FALSE);
#endif

#ifdef ESRCH
    VariableDefinePlatformVar(pc, nullptr, "ESRCH", &pc->IntType, (union AnyValue *)&ESRCHValue, FALSE);
#endif

#ifdef ESTALE
    VariableDefinePlatformVar(pc, nullptr, "ESTALE", &pc->IntType, (union AnyValue *)&ESTALEValue, FALSE);
#endif

#ifdef ETIME
    VariableDefinePlatformVar(pc, nullptr, "ETIME", &pc->IntType, (union AnyValue *)&ETIMEValue, FALSE);
#endif

#ifdef ETIMEDOUT
    VariableDefinePlatformVar(pc, nullptr, "ETIMEDOUT", &pc->IntType, (union AnyValue *)&ETIMEDOUTValue, FALSE);
#endif

#ifdef ETXTBSY
    VariableDefinePlatformVar(pc, nullptr, "ETXTBSY", &pc->IntType, (union AnyValue *)&ETXTBSYValue, FALSE);
#endif

#ifdef EWOULDBLOCK
    VariableDefinePlatformVar(pc, nullptr, "EWOULDBLOCK", &pc->IntType, (union AnyValue *)&EWOULDBLOCKValue, FALSE);
#endif

#ifdef EXDEV
    VariableDefinePlatformVar(pc, nullptr, "EXDEV", &pc->IntType, (union AnyValue *)&EXDEVValue, FALSE);
#endif

    VariableDefinePlatformVar(pc, nullptr, "errno", &pc->IntType, (union AnyValue *)&errno, TRUE);
}

#endif /* !BUILTIN_MINI_STDLIB */
