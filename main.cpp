#include <utility>
#include <unistd.h>

class SocketHandle {
private:
	int fd_ = -1;

public:
	SocketHandle() = default;

	explicit SocketHandle(int fd) : fd_(fd) {}

	~SocketHandle(){
		reset();
	}

	SocketHandle(const SocketHandle&) = delete;
	SocketHandle& operator=(const SocketHandle&) = delete;

	SocketHandle(SocketHandle&& other)	noexcept : fd_(std::exchange(other.fd_, -1)) {}

	SocketHandle& operator=(SocketHandle&& other) noexcept {
		if (this == &other)
			return *this;
		
		reset(exchange(other.fd_, -1));
		return *this;
	}

	int get() const {
		return fd_;
	}

	bool valid() const{
		return fd_ != -1;
	}

    void reset(int new_fd = -1){
        if (fd_ != -1)
            ::close(fd_);

        fd_ = new_fd;
    }
};