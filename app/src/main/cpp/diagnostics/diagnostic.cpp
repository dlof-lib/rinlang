#include "diagnostic.h"

namespace rin::diag {

std::string codeString(Code c) {
    switch (c) {
        case Code::E0001_UndefinedVariable:      return "E0001";
        case Code::E0002_DuplicateVariable:      return "E0002";
        case Code::E0003_InvalidAssignment:      return "E0003";
        case Code::E0004_InvalidType:            return "E0004";
        case Code::E0005_TypeMismatch:           return "E0005";
        case Code::E0006_UnknownFunction:        return "E0006";
        case Code::E0007_InvalidArguments:       return "E0007";
        case Code::E0008_InvalidReturn:          return "E0008";
        case Code::E0009_MissingReturn:          return "E0009";
        case Code::E0010_ParserError:            return "E0010";
        case Code::E0011_UnexpectedToken:        return "E0011";
        case Code::E0012_MissingToken:           return "E0012";
        case Code::E0013_InvalidExpression:      return "E0013";
        case Code::E0014_InvalidContainer:       return "E0014";
        case Code::E0015_UnknownContainer:       return "E0015";
        case Code::E0016_InvalidProperty:        return "E0016";
        case Code::E0017_UndefinedProperty:      return "E0017";
        case Code::E0018_InvalidSchema:          return "E0018";
        case Code::E0019_SchemaViolation:        return "E0019";
        case Code::E0020_InvalidDocument:        return "E0020";
        case Code::E0021_InvalidIndex:           return "E0021";
        case Code::E0022_InvalidRelation:        return "E0022";
        case Code::E0023_TransactionError:       return "E0023";
        case Code::E0024_MigrationError:         return "E0024";
        case Code::E0025_CacheError:             return "E0025";
        case Code::E0026_AsyncError:             return "E0026";
        case Code::E0027_AwaitOutsideAsync:      return "E0027";
        case Code::E0028_ImportError:            return "E0028";
        case Code::E0029_ModuleNotFound:         return "E0029";
        case Code::E0030_CircularDependency:     return "E0030";
        case Code::E0031_GenericError:           return "E0031";
        case Code::E0032_InvalidGenericArgument: return "E0032";
        case Code::E0033_OwnershipError:         return "E0033";
        case Code::E0034_BorrowError:            return "E0034";
        case Code::E0035_RuntimeError:           return "E0035";
        case Code::E0036_IOFailure:              return "E0036";
        case Code::E0037_NetworkError:           return "E0037";
        case Code::E0038_PackageError:           return "E0038";
        case Code::E0039_InternalCompilerError:  return "E0039";
        case Code::E0040_UnsupportedFeature:     return "E0040";

        case Code::W0001_UnusedVariable:         return "W0001";
        case Code::W0002_UnusedImport:           return "W0002";
        case Code::W0003_UnreachableCode:        return "W0003";
        case Code::W0004_DeprecatedFeature:      return "W0004";
        case Code::W0005_ShadowedVariable:       return "W0005";
        case Code::W0006_UnnecessaryConversion:  return "W0006";
        case Code::W0007_UnusedFunction:         return "W0007";
        case Code::W0008_SuspiciousComparison:   return "W0008";
    }
    return "E0031";
}

std::string codeName(Code c) {
    switch (c) {
        case Code::E0001_UndefinedVariable:      return "UndefinedVariable";
        case Code::E0002_DuplicateVariable:      return "DuplicateVariable";
        case Code::E0003_InvalidAssignment:      return "InvalidAssignment";
        case Code::E0004_InvalidType:            return "InvalidType";
        case Code::E0005_TypeMismatch:           return "TypeMismatch";
        case Code::E0006_UnknownFunction:        return "UnknownFunction";
        case Code::E0007_InvalidArguments:       return "InvalidArguments";
        case Code::E0008_InvalidReturn:          return "InvalidReturn";
        case Code::E0009_MissingReturn:          return "MissingReturn";
        case Code::E0010_ParserError:            return "ParserError";
        case Code::E0011_UnexpectedToken:        return "UnexpectedToken";
        case Code::E0012_MissingToken:           return "MissingToken";
        case Code::E0013_InvalidExpression:      return "InvalidExpression";
        case Code::E0014_InvalidContainer:       return "InvalidContainer";
        case Code::E0015_UnknownContainer:       return "UnknownContainer";
        case Code::E0016_InvalidProperty:        return "InvalidProperty";
        case Code::E0017_UndefinedProperty:      return "UndefinedProperty";
        case Code::E0018_InvalidSchema:          return "InvalidSchema";
        case Code::E0019_SchemaViolation:        return "SchemaViolation";
        case Code::E0020_InvalidDocument:        return "InvalidDocument";
        case Code::E0021_InvalidIndex:           return "InvalidIndex";
        case Code::E0022_InvalidRelation:        return "InvalidRelation";
        case Code::E0023_TransactionError:       return "TransactionError";
        case Code::E0024_MigrationError:         return "MigrationError";
        case Code::E0025_CacheError:             return "CacheError";
        case Code::E0026_AsyncError:             return "AsyncError";
        case Code::E0027_AwaitOutsideAsync:      return "AwaitOutsideAsync";
        case Code::E0028_ImportError:            return "ImportError";
        case Code::E0029_ModuleNotFound:         return "ModuleNotFound";
        case Code::E0030_CircularDependency:     return "CircularDependency";
        case Code::E0031_GenericError:           return "GenericError";
        case Code::E0032_InvalidGenericArgument: return "InvalidGenericArgument";
        case Code::E0033_OwnershipError:         return "OwnershipError";
        case Code::E0034_BorrowError:            return "BorrowError";
        case Code::E0035_RuntimeError:           return "RuntimeError";
        case Code::E0036_IOFailure:              return "IOFailure";
        case Code::E0037_NetworkError:           return "NetworkError";
        case Code::E0038_PackageError:           return "PackageError";
        case Code::E0039_InternalCompilerError:  return "InternalCompilerError";
        case Code::E0040_UnsupportedFeature:     return "UnsupportedFeature";

        case Code::W0001_UnusedVariable:         return "UnusedVariable";
        case Code::W0002_UnusedImport:           return "UnusedImport";
        case Code::W0003_UnreachableCode:        return "UnreachableCode";
        case Code::W0004_DeprecatedFeature:      return "DeprecatedFeature";
        case Code::W0005_ShadowedVariable:       return "ShadowedVariable";
        case Code::W0006_UnnecessaryConversion:  return "UnnecessaryConversion";
        case Code::W0007_UnusedFunction:         return "UnusedFunction";
        case Code::W0008_SuspiciousComparison:   return "SuspiciousComparison";
    }
    return "GenericError";
}

Severity defaultSeverity(Code c) {
    return codeString(c)[0] == 'W' ? Severity::Warning : Severity::Error;
}

} // namespace rin::diag
