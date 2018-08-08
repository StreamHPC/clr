//
// Copyright (c) 2008 Advanced Micro Devices, Inc. All rights reserved.
//

#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <iterator>

#include "os/os.hpp"
#include "device/pal/paldevice.hpp"
#include "device/pal/palprogram.hpp"
#include "device/pal/palkernel.hpp"
#include "utils/options.hpp"
#if defined(WITH_LIGHTNING_COMPILER)
#include "driver/AmdCompiler.h"
#include "opencl1.2-c.amdgcn.inc"
#include "opencl2.0-c.amdgcn.inc"
#endif  // !defined(WITH_LIGHTNING_COMPILER)
#include <cstdio>

#if defined(ATI_OS_LINUX)
#include <dlfcn.h>
#include <libgen.h>
#endif  // defined(ATI_OS_LINUX)
#if defined(ATI_OS_WIN)
#include <windows.h>
#endif  // defined(ATI_OS_WIN)

// CLC_IN_PROCESS_CHANGE
extern int openclFrontEnd(const char* cmdline, std::string*, std::string* typeInfo = nullptr);

namespace pal {

bool HSAILProgram::compileImpl(const std::string& sourceCode,
                               const std::vector<const std::string*>& headers,
                               const char** headerIncludeNames, amd::option::Options* options) {
#if defined(WITH_LIGHTNING_COMPILER)
  assert(!"Should not reach here");
#else  // !defined(WITH_LIGHTNING_COMPILER)
  acl_error errorCode;
  aclTargetInfo target;

  std::string arch = "hsail";
  if (dev().settings().use64BitPtr_) {
    arch += "64";
  }
  target = aclGetTargetInfo(arch.c_str(), dev().hwInfo()->targetName_, &errorCode);

  // end if asic info is ready
  // We dump the source code for each program (param: headers)
  // into their filenames (headerIncludeNames) into the TEMP
  // folder specific to the OS and add the include path while
  // compiling

  // Find the temp folder for the OS
  std::string tempFolder = amd::Os::getTempPath();

  // Iterate through each source code and dump it into tmp
  std::fstream f;
  std::vector<std::string> newDirs;
  for (size_t i = 0; i < headers.size(); ++i) {
    std::string headerPath = tempFolder;
    std::string headerIncludeName(headerIncludeNames[i]);
    // replace / in path with current os's file separator
    if (amd::Os::fileSeparator() != '/') {
      for (auto& it : headerIncludeName) {
        if (it == '/') it = amd::Os::fileSeparator();
      }
    }
    size_t pos = headerIncludeName.rfind(amd::Os::fileSeparator());
    if (pos != std::string::npos) {
      headerPath += amd::Os::fileSeparator();
      headerPath += headerIncludeName.substr(0, pos);
      headerIncludeName = headerIncludeName.substr(pos + 1);
    }
    if (!amd::Os::pathExists(headerPath)) {
      bool ret = amd::Os::createPath(headerPath);
      assert(ret && "failed creating path!");
      newDirs.push_back(headerPath);
    }
    std::string headerFullName = headerPath + amd::Os::fileSeparator() + headerIncludeName;
    f.open(headerFullName.c_str(), std::fstream::out);
    // Should we allow asserts
    assert(!f.fail() && "failed creating header file!");
    f.write(headers[i]->c_str(), headers[i]->length());
    f.close();
  }

  // Create Binary
  binaryElf_ = aclBinaryInit(sizeof(aclBinary), &target, &binOpts_, &errorCode);
  if (errorCode != ACL_SUCCESS) {
    buildLog_ += "Error: aclBinary init failure\n";
    LogWarning("aclBinaryInit failed");
    return false;
  }

  // Insert opencl into binary
  errorCode = aclInsertSection(dev().compiler(), binaryElf_, sourceCode.c_str(),
                               strlen(sourceCode.c_str()), aclSOURCE);
  if (errorCode != ACL_SUCCESS) {
    buildLog_ += "Error: Inserting openCl Source to binary\n";
  }

  // Set the options for the compiler
  // Set the include path for the temp folder that contains the includes
  if (!headers.empty()) {
    compileOptions_.append(" -I");
    compileOptions_.append(tempFolder);
  }

#if !defined(_LP64) && defined(ATI_OS_LINUX)
  if (options->origOptionStr.find("-cl-std=CL2.0") != std::string::npos &&
      !dev().settings().force32BitOcl20_) {
    errorCode = ACL_UNSUPPORTED;
    LogWarning("aclCompile failed");
    return false;
  }
#endif

  // Compile source to IR
  compileOptions_.append(hsailOptions(options));
  errorCode = aclCompile(dev().compiler(), binaryElf_, compileOptions_.c_str(), ACL_TYPE_OPENCL,
                         ACL_TYPE_LLVMIR_BINARY, nullptr);
  buildLog_ += aclGetCompilerLog(dev().compiler());
  if (errorCode != ACL_SUCCESS) {
    LogWarning("aclCompile failed");
    buildLog_ += "Error: Compiling CL to IR\n";
    return false;
  }

  clBinary()->storeCompileOptions(compileOptions_);

  // Save the binary in the interface class
  saveBinaryAndSetType(TYPE_COMPILED);
#endif  // !defined(WITH_LIGHTNING_COMPILER)
  return true;
}

#if defined(WITH_LIGHTNING_COMPILER)
static std::string llvmBin_(amd::Os::getEnvironment("LLVM_BIN"));

#if defined(ATI_OS_WIN)
static BOOL CALLBACK checkLLVM_BIN(PINIT_ONCE InitOnce, PVOID Parameter, PVOID* lpContex) {
  if (llvmBin_.empty()) {
    HMODULE hm = NULL;
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)&amd::Device::init, &hm)) {
      char path[1024];
      GetModuleFileNameA(hm, path, sizeof(path));
      llvmBin_ = path;
      size_t pos = llvmBin_.rfind('\\');
      if (pos != std::string::npos) {
        llvmBin_.resize(pos);
      }
    }
  }
  return TRUE;
}
#endif  // defined (ATI_OS_WINDOWS)

#if defined(ATI_OS_LINUX)
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void checkLLVM_BIN() {
  if (llvmBin_.empty()) {
    Dl_info info;
    if (dladdr((const void*)&amd::Device::init, &info)) {
      char* str = strdup(info.dli_fname);
      if (str) {
        llvmBin_ = dirname(str);
        free(str);
        size_t pos = llvmBin_.rfind("lib");
        if (pos != std::string::npos) {
          llvmBin_.replace(pos, 3, "bin");
        }
      }
    }
  }
}
#endif  // defined(ATI_OS_LINUX)

std::unique_ptr<amd::opencl_driver::Compiler> LightningProgram::newCompilerInstance() {
#if defined(ATI_OS_WIN)
  static INIT_ONCE initOnce;
  InitOnceExecuteOnce(&initOnce, checkLLVM_BIN, NULL, NULL);
#endif  // defined(ATI_OS_WIN)
#if defined(ATI_OS_LINUX)
  pthread_once(&once, checkLLVM_BIN);
#endif  // defined(ATI_OS_LINUX)
#if defined(DEBUG)
  std::string clangExe(llvmBin_ + LINUX_SWITCH("/clang", "\\clang.exe"));
  struct stat buf;
  if (stat(clangExe.c_str(), &buf)) {
    std::string msg("Could not find the Clang binary in " + llvmBin_);
    LogWarning(msg.c_str());
  }
#endif  // defined(DEBUG)

  return std::unique_ptr<amd::opencl_driver::Compiler>(
      amd::opencl_driver::CompilerFactory().CreateAMDGPUCompiler(llvmBin_));
}

bool LightningProgram::compileImpl(const std::string& sourceCode,
                                   const std::vector<const std::string*>& headers,
                                   const char** headerIncludeNames, amd::option::Options* options) {
  using namespace amd::opencl_driver;
  std::unique_ptr<Compiler> C(newCompilerInstance());
  std::vector<Data*> inputs;

  Data* input = C->NewBufferReference(DT_CL, sourceCode.c_str(), sourceCode.length());
  if (input == NULL) {
    buildLog_ += "Error while creating data from source code";
    return false;
  }

  inputs.push_back(input);

  amd::opencl_driver::Buffer* output = C->NewBuffer(DT_LLVM_BC);
  if (output == NULL) {
    buildLog_ += "Error while creating buffer for the LLVM bitcode";
    return false;
  }

  // Set the options for the compiler
  // Some options are set in Clang AMDGPUToolChain (like -m64)
  std::ostringstream ostrstr;
  std::copy(options->clangOptions.begin(), options->clangOptions.end(),
            std::ostream_iterator<std::string>(ostrstr, " "));

  std::string driverOptions(ostrstr.str());

  const char* xLang = options->oVariables->XLang;
  if (xLang != NULL && strcmp(xLang, "cl")) {
    buildLog_ += "Unsupported OpenCL language.\n";
  }

  // FIXME_Nikolay: the program manager should be setting the language
  // driverOptions.append(" -x cl");

  driverOptions.append(" -cl-std=").append(options->oVariables->CLStd);

  // Set the -O#
  std::ostringstream optLevel;
  optLevel << " -O" << options->oVariables->OptLevel;
  driverOptions.append(optLevel.str());

  // Set the machine target
  std::ostringstream mCPU;
  mCPU << " -mcpu=gfx" << dev().hwInfo()->gfxipVersion_;
  driverOptions.append(mCPU.str());

  // Set xnack option if needed
  if (dev().hwInfo()->xnackEnabled_) {
    driverOptions.append(" -mxnack");
  }

  driverOptions.append(options->llvmOptions);
  driverOptions.append(hsailOptions(options));

  // Set whole program mode
  driverOptions.append(" -mllvm -amdgpu-early-inline-all -mllvm -amdgpu-prelink");

  // Find the temp folder for the OS
  std::string tempFolder = amd::Os::getEnvironment("TEMP");
  if (tempFolder.empty()) {
    tempFolder = amd::Os::getEnvironment("TMP");
    if (tempFolder.empty()) {
      tempFolder = WINDOWS_SWITCH(".", "/tmp");
      ;
    }
  }
  // Iterate through each source code and dump it into tmp
  std::fstream f;
  std::vector<std::string> headerFileNames(headers.size());
  std::vector<std::string> newDirs;
  for (size_t i = 0; i < headers.size(); ++i) {
    std::string headerPath = tempFolder;
    std::string headerIncludeName(headerIncludeNames[i]);
    // replace / in path with current os's file separator
    if (amd::Os::fileSeparator() != '/') {
      for (auto& it : headerIncludeName) {
        if (it == '/') it = amd::Os::fileSeparator();
      }
    }
    size_t pos = headerIncludeName.rfind(amd::Os::fileSeparator());
    if (pos != std::string::npos) {
      headerPath += amd::Os::fileSeparator();
      headerPath += headerIncludeName.substr(0, pos);
      headerIncludeName = headerIncludeName.substr(pos + 1);
    }
    if (!amd::Os::pathExists(headerPath)) {
      bool ret = amd::Os::createPath(headerPath);
      assert(ret && "failed creating path!");
      newDirs.push_back(headerPath);
    }
    std::string headerFullName = headerPath + amd::Os::fileSeparator() + headerIncludeName;
    headerFileNames[i] = headerFullName;
    f.open(headerFullName.c_str(), std::fstream::out);
    // Should we allow asserts
    assert(!f.fail() && "failed creating header file!");
    f.write(headers[i]->c_str(), headers[i]->length());
    f.close();

    Data* inc = C->NewFileReference(DT_CL_HEADER, headerFileNames[i]);
    if (inc == NULL) {
      buildLog_ += "Error while creating data from headers";
      return false;
    }
    inputs.push_back(inc);
  }

  // Set the include path for the temp folder that contains the includes
  if (!headers.empty()) {
    driverOptions.append(" -I");
    driverOptions.append(tempFolder);
  }

  if (options->isDumpFlagSet(amd::option::DUMP_CL)) {
    std::ofstream f(options->getDumpFileName(".cl").c_str(), std::ios::trunc);
    if (f.is_open()) {
      f << "/* Compiler options:\n"
           "-c -emit-llvm -target amdgcn-amd-amdhsa-opencl -x cl "
        << driverOptions << " -include opencl-c.h "
        << "\n*/\n\n"
        << sourceCode;
      f.close();
    } else {
      buildLog_ += "Warning: opening the file to dump the OpenCL source failed.\n";
    }
  }

  // FIXME_lmoriche: has the CL option been validated?
  uint clcStd =
      (options->oVariables->CLStd[2] - '0') * 100 + (options->oVariables->CLStd[4] - '0') * 10;

  std::pair<const void*, size_t> hdr;
  switch (clcStd) {
    case 100:
    case 110:
    case 120:
      hdr = {opencl1_2_c_amdgcn, opencl1_2_c_amdgcn_size};
      break;
    case 200:
      hdr = {opencl2_0_c_amdgcn, opencl2_0_c_amdgcn_size};
      break;
    default:
      buildLog_ += "Unsupported requested OpenCL C version (-cl-std).\n";
      return false;
  }

  File* pch = C->NewTempFile(DT_CL_HEADER);
  if (pch == NULL || !pch->WriteData((const char*)hdr.first, hdr.second)) {
    buildLog_ += "Error while opening the opencl-c header ";
    return false;
  }

  driverOptions.append(" -include-pch " + pch->Name());
  driverOptions.append(" -Xclang -fno-validate-pch");

  // Tokenize the options string into a vector of strings
  std::istringstream istrstr(driverOptions);
  std::istream_iterator<std::string> sit(istrstr), end;
  std::vector<std::string> params(sit, end);

  // Compile source to IR
  bool ret =
      dev().cacheCompilation()->compileToLLVMBitcode(C.get(), inputs, output, params, buildLog_);
  buildLog_ += C->Output();
  if (!ret) {
    buildLog_ += "Error: Failed to compile opencl source (from CL to LLVM IR).\n";
    return false;
  }

  llvmBinary_.assign(output->Buf().data(), output->Size());
  elfSectionType_ = amd::OclElf::LLVMIR;

  if (options->isDumpFlagSet(amd::option::DUMP_BC_ORIGINAL)) {
    std::ofstream f(options->getDumpFileName("_original.bc").c_str(),
                    std::ios::binary | std::ios::trunc);
    if (f.is_open()) {
      f.write(llvmBinary_.data(), llvmBinary_.size());
      f.close();
    } else {
      buildLog_ += "Warning: opening the file to dump the compiled IR failed.\n";
    }
  }

  if (clBinary()->saveSOURCE()) {
    clBinary()->elfOut()->addSection(amd::OclElf::SOURCE, sourceCode.data(), sourceCode.size());
  }
  if (clBinary()->saveLLVMIR()) {
    clBinary()->elfOut()->addSection(amd::OclElf::LLVMIR, llvmBinary_.data(), llvmBinary_.size(),
                                     false);
    // store the original compile options
    clBinary()->storeCompileOptions(compileOptions_);
  }
  return true;
}
#endif  // defined(WITH_LIGHTNING_COMPILER)

}  // namespace pal
