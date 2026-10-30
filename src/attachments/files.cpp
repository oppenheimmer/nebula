#include "nebula/attachments/files.hpp"
#include <QCryptographicHash>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nebula::files {
namespace {
[[noreturn]] void failure(const std::string& action) { throw std::runtime_error(action+": "+std::strerror(errno)); }
class Descriptor {
public:
    explicit Descriptor(int fd):fd_(fd) { if (fd<0) failure("Cannot open local file"); }
    ~Descriptor() { ::close(fd_); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    operator int() const { return fd_; }
private:
    int fd_;
};
struct stat regular(int fd) {
    struct stat status{};
    if (::fstat(fd,&status)<0) failure("Cannot inspect local file");
    if (!S_ISREG(status.st_mode)) throw std::runtime_error("Only regular files are supported");
    return status;
}
void write_all(int fd,const char* bytes,std::size_t length) {
    while (length) {
        const auto written=::write(fd,bytes,length);
        if (written<0 && errno==EINTR) continue;
        if (written<=0) failure("Cannot write managed file");
        bytes+=written; length-=std::size_t(written);
    }
}
Digest read_file(int input,int output) {
    const auto before=regular(input); QCryptographicHash hash(QCryptographicHash::Sha256);
    std::array<char,65536> buffer{}; std::int64_t size=0;
    while (true) {
        const auto count=::read(input,buffer.data(),buffer.size());
        if (count<0 && errno==EINTR) continue;
        if (count<0) failure("Cannot read local file");
        if (!count) break;
        hash.addData(QByteArrayView(buffer.data(),qsizetype(count))); size+=count;
        if (output>=0) write_all(output,buffer.data(),std::size_t(count));
    }
    const auto after=regular(input);
#if defined(__APPLE__)
    const bool changed=before.st_mtimespec.tv_sec!=after.st_mtimespec.tv_sec || before.st_mtimespec.tv_nsec!=after.st_mtimespec.tv_nsec;
#else
    const bool changed=before.st_mtim.tv_sec!=after.st_mtim.tv_sec || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec;
#endif
    if (changed || before.st_size!=after.st_size || size!=after.st_size)
        throw std::runtime_error("The source file changed while being read; retry the import or backup");
    return {size,hash.result().toHex().toStdString()};
}
} // namespace
Digest digest(const std::filesystem::path& path) {
    Descriptor input(::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));
    return read_file(input,-1);
}
Digest copy_new(const std::filesystem::path& source,const std::filesystem::path& destination) {
    Descriptor input(::open(source.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK)); regular(input);
    Descriptor output(::open(destination.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
    try {
        auto result=read_file(input,output);
        if (::fsync(output)<0) failure("Cannot persist managed file");
        return result;
    } catch (...) { std::error_code ignored; std::filesystem::remove(destination,ignored); throw; }
}
void sync_directory(const std::filesystem::path& directory) {
    Descriptor fd(::open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC));
    if (::fsync(fd)<0) failure("Cannot persist workspace directory");
}
void write_new(const std::filesystem::path& path,const std::string& text) {
    Descriptor output(::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));
    try { write_all(output,text.data(),text.size()); if (::fsync(output)<0) failure("Cannot persist local file"); }
    catch (...) { std::error_code ignored; std::filesystem::remove(path,ignored); throw; }
}
void initialize_lock(const std::filesystem::path& directory) {
    Descriptor file(::open((directory/".nebula.lock").c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600));
    regular(file); if (::fsync(file)<0) failure("Cannot persist workspace lock file"); sync_directory(directory);
}
WorkspaceLock::WorkspaceLock(const std::filesystem::path& directory,bool exclusive) {
    descriptor_=::open((directory/".nebula.lock").c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if (descriptor_<0) failure("Cannot open workspace attachment lock");
    try {
        regular(descriptor_);
        while (::flock(descriptor_,(exclusive?LOCK_EX:LOCK_SH)|LOCK_NB)<0)
            if (errno!=EINTR) throw std::runtime_error("Workspace files are busy; retry after import, cleanup or backup finishes");
    } catch (...) { ::close(descriptor_); descriptor_=-1; throw; }
}
WorkspaceLock::~WorkspaceLock() { if (descriptor_>=0) { ::flock(descriptor_,LOCK_UN); ::close(descriptor_); } }
} // namespace nebula::files
