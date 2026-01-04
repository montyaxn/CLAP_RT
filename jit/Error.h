#pragma once

#include <llvm/Support/Error.h>
#include <string>
#include <system_error>

namespace clap_rt {

enum class ErrorCode {
  TargetCreationFailed,
  CompilationFailed,
  ModuleGenerationFailed,
  SymbolNotFound
};

inline const std::error_category &ClapErrorCategory() {
  class ClapErrorCategoryImpl : public std::error_category {
  public:
    const char *name() const noexcept override { return "clap"; }

    std::string message(int ev) const override {
      switch (static_cast<ErrorCode>(ev)) {
      case ErrorCode::TargetCreationFailed:
        return "Failed to create target";
      case ErrorCode::CompilationFailed:
        return "Compilation failed";
      case ErrorCode::ModuleGenerationFailed:
        return "Failed to generate module";
      case ErrorCode::SymbolNotFound:
        return "Symbol not found";
      default:
        return "Unknown error";
      }
    }
  };

  static ClapErrorCategoryImpl instance;
  return instance;
}

inline std::error_code make_error_code(ErrorCode e) {
  return {static_cast<int>(e), ClapErrorCategory()};
}

inline llvm::Error makeError(ErrorCode code, llvm::StringRef message,
                             llvm::StringRef filePath = "") {
  std::string fullMsg = ClapErrorCategory().message(static_cast<int>(code));
  if (!message.empty())
    fullMsg += ": " + message.str();
  if (!filePath.empty())
    fullMsg += " [file: " + filePath.str() + "]";

  return llvm::make_error<llvm::StringError>(fullMsg, make_error_code(code));
}

} // namespace clap_rt
