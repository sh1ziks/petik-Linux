#include <sys/socket.h>
#include <fcntl.h>
#include <cassert>
#include <cerrno>
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
		
		reset(std::exchange(other.fd_, -1));
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

int main() {
    int raw = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(raw >= 0);
    {
        SocketHandle a(raw);
        assert(a.valid());

        SocketHandle b(std::move(a));
        assert(!a.valid());                  // источник опустел
        assert(b.valid() && b.get() == raw);

        SocketHandle c;
        c = std::move(b);                    // присваивание в пустой объект
        assert(c.get() == raw && !b.valid());
    }                                        // здесь fd должен закрыться
    assert(fcntl(raw, F_GETFD) == -1 && errno == EBADF);   // fd действительно закрыт
}