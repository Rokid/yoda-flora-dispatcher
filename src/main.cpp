#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "flora-svc.h"
#include "clargs.h"
#include "rlog.h"
#include "defs.h"

#define MACRO_TO_STRING(x) MACRO_TO_STRING1(x)
#define MACRO_TO_STRING1(x) #x

using namespace flora;
using namespace std;

static void print_prompt(const char* progname) {
  static const char* prompt =
    "USAGE: %s [options]\n"
    "options:\n"
    "\t--help    打印帮助信息\n"
    "\t--version    版本号\n"
    "\t--uri=*    指定flora服务uri\n"
    "\t--msg-buf-size=*    指定flora消息缓冲区大小\n"
    "\t--log-file=*    指定log输出文件路径\n"
    "\t--log-service-port=*    指定log服务端口"
    ;
  KLOGI(TAG, prompt, progname);
}

class CmdlineArgs {
public:
  CmdlineArgs() {
    uris.reserve(2);
  }

  void add_uri(const char* uri) {
    uris.emplace_back(uri);
  }

  uint32_t msg_buf_size = 0;
  vector<string> uris;
  string log_file;
  int32_t log_port = 0;
};

void run(CmdlineArgs& args);

static bool parse_cmdline(shared_ptr<CLArgs> &clargs, CmdlineArgs& res) {
  int32_t iv;
  uint32_t i;
  CLPair pair;

  for (i = 1; i < clargs->size(); ++i) {
    clargs->at(i, pair);
    if (pair.match("msg-buf-size")) {
      if (!pair.to_integer(iv))
        goto invalid_option;
      res.msg_buf_size = iv;
    } else if (pair.match("uri")) {
      if (pair.value == nullptr || pair.value[0] == '\0')
        goto invalid_option;
      res.add_uri(pair.value);
    } else if (pair.match("log-file")) {
      res.log_file = pair.value;
    } else if (pair.match("log-service-port")) {
      if (!pair.to_integer(iv))
        goto invalid_option;
      res.log_port = iv;
    } else {
      goto invalid_option;
    }
  }
  if (res.uris.empty()) {
    KLOGE(TAG, "missing service uri");
    return false;
  }
  return true;

invalid_option:
  if (pair.value)
    KLOGE(TAG, "invalid option: --%s=%s", pair.key, pair.value);
  else
    KLOGE(TAG, "invalid option: --%s", pair.key);
  return false;
}

int main(int argc, char** argv) {
  shared_ptr<CLArgs> h = CLArgs::parse(argc, argv);
  if (h == nullptr || h->find("help", nullptr, nullptr)) {
    print_prompt(argv[0]);
    return 0;
  }
  if (h->find("version", nullptr, nullptr)) {
    KLOGI(TAG, "git commit id: %s", MACRO_TO_STRING(GIT_COMMIT_ID));
    return 0;
  }
  CmdlineArgs cmdargs;
  if (!parse_cmdline(h, cmdargs)) {
    return 1;
  }
  h.reset();

  run(cmdargs);
  return 0;
}

static bool set_log_file(const std::string& file) {
  if (file.length() == 0)
    return false;
  int fd = open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd < 0)
    return false;
  rokid_log_ctl(ROKID_LOG_CTL_DEFAULT_ENDPOINT, "file", fd);
  return true;
}

static void set_log_port(int32_t port) {
  if (port > 0) {
    TCPSocketArg arg;
    arg.host = "0.0.0.0";
    arg.port = port;
    rokid_log_ctl(ROKID_LOG_CTL_DEFAULT_ENDPOINT, "tcp-socket", &arg);
  }
}

void run(CmdlineArgs& args) {
  if (!set_log_file(args.log_file))
    set_log_port(args.log_port);

  KLOGI(TAG, "msg buf size = %u, uri count %d", args.msg_buf_size,
      args.uris.size());
  shared_ptr<Dispatcher> disp = Dispatcher::new_instance(
      FLORA_DISP_FLAG_MONITOR, args.msg_buf_size);
  size_t i;
  vector<shared_ptr<Poll> > polls;
  polls.reserve(2);
  for (i = 0; i < args.uris.size(); ++i) {
    KLOGI(TAG, "uri = %s", args.uris[i].c_str());
    shared_ptr<Poll> tpoll = Poll::new_instance(args.uris[i].c_str());
    if (tpoll.get() == nullptr) {
      KLOGE(TAG, "create poll failed for uri %s", args.uris[i].c_str());
      return;
    }
    tpoll->start(disp);
    polls.push_back(tpoll);
  }
  disp->run(true);
  for (i = 0; i < polls.size(); ++i)
    polls[i]->stop();
}
