// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/domain_reliability/util.h"

#include <stddef.h>

#include "base/callback.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/net_errors.h"

namespace domain_reliability {

namespace {

const struct NetErrorMapping {
  int net_error;
  const char* beacon_status;
} net_error_map[] = {
  { net::OK, "ok" },
  { net::ERR_ABORTED, "aborted" },
  { net::ERR_TIMED_OUT, "tcp.connection.timed_out" },
  { net::ERR_CONNECTION_CLOSED, "tcp.connection.closed" },
  { net::ERR_CONNECTION_RESET, "tcp.connection.reset" },
  { net::ERR_CONNECTION_REFUSED, "tcp.connection.refused" },
  { net::ERR_CONNECTION_ABORTED, "tcp.connection.aborted" },
  { net::ERR_CONNECTION_FAILED, "tcp.connection.failed" },
  { net::ERR_NAME_NOT_RESOLVED, "dns" },
  { net::ERR_SSL_PROTOCOL_ERROR, "ssl.protocol.error" },
  { net::ERR_ADDRESS_INVALID, "tcp.connection.address_invalid" },
  { net::ERR_ADDRESS_UNREACHABLE, "tcp.connection.address_unreachable" },
  { net::ERR_CONNECTION_TIMED_OUT, "tcp.connection.timed_out" },
  { net::ERR_NAME_RESOLUTION_FAILED, "dns" },
  { net::ERR_SSL_PINNED_KEY_NOT_IN_CERT_CHAIN,
        "ssl.cert.pinned_key_not_in_cert_chain" },
  { net::ERR_CERT_COMMON_NAME_INVALID, "ssl.cert.name_invalid" },
  { net::ERR_CERT_DATE_INVALID, "ssl.cert.date_invalid" },
  { net::ERR_CERT_AUTHORITY_INVALID, "ssl.cert.authority_invalid" },
  { net::ERR_CERT_REVOKED, "ssl.cert.revoked" },
  { net::ERR_CERT_INVALID, "ssl.cert.invalid" },
  { net::ERR_EMPTY_RESPONSE, "http.response.empty" },
  { net::ERR_SPDY_PING_FAILED, "spdy.ping_failed" },
  { net::ERR_SPDY_PROTOCOL_ERROR, "spdy.protocol" },
  { net::ERR_QUIC_PROTOCOL_ERROR, "quic.protocol" },
  { net::ERR_DNS_MALFORMED_RESPONSE, "dns.protocol" },
  { net::ERR_DNS_SERVER_FAILED, "dns.server" },
  { net::ERR_DNS_TIMED_OUT, "dns.timed_out" },
  { net::ERR_INSECURE_RESPONSE, "ssl" },
  { net::ERR_CONTENT_LENGTH_MISMATCH, "http.response.content_length_mismatch" },
  { net::ERR_INCOMPLETE_CHUNKED_ENCODING,
        "http.response.incomplete_chunked_encoding" },
  { net::ERR_SSL_VERSION_OR_CIPHER_MISMATCH,
        "ssl.version_or_cipher_mismatch" },
  { net::ERR_BAD_SSL_CLIENT_AUTH_CERT, "ssl.bad_client_auth_cert" },
  { net::ERR_INVALID_CHUNKED_ENCODING,
        "http.response.invalid_chunked_encoding" },
  { net::ERR_RESPONSE_HEADERS_TRUNCATED, "http.response.headers.truncated" },
  { net::ERR_REQUEST_RANGE_NOT_SATISFIABLE,
        "http.request.range_not_satisfiable" },
  { net::ERR_INVALID_RESPONSE, "http.response.invalid" },
  { net::ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_DISPOSITION,
        "http.response.headers.multiple_content_disposition" },
  { net::ERR_RESPONSE_HEADERS_MULTIPLE_CONTENT_LENGTH,
        "http.response.headers.multiple_content_length" },
  { net::ERR_SSL_UNRECOGNIZED_NAME_ALERT, "ssl.unrecognized_name_alert" }
};

bool CanReportFullBeaconURLToCollector(const GURL& beacon_url,
                                       const GURL& collector_url) {
  return beacon_url.GetOrigin() == collector_url.GetOrigin();
}

}  // namespace

// static
bool GetDomainReliabilityBeaconStatus(
    int net_error,
    int http_response_code,
    std::string* beacon_status_out) {
  if (net_error == net::OK) {
    if (http_response_code >= 400 && http_response_code < 600)
      *beacon_status_out = "http.error";
    else
      *beacon_status_out = "ok";
    return true;
  }

  // TODO(juliatuttle): Consider sorting and using binary search?
  for (size_t i = 0; i < arraysize(net_error_map); i++) {
    if (net_error_map[i].net_error == net_error) {
      *beacon_status_out = net_error_map[i].beacon_status;
      return true;
    }
  }
  return false;
}

// TODO(juliatuttle): Consider using NPN/ALPN instead, if there's a good way to
//                    differentiate HTTP and HTTPS.
std::string GetDomainReliabilityProtocol(
    net::HttpResponseInfo::ConnectionInfo connection_info,
    bool ssl_info_populated) {
  switch (connection_info) {
    case net::HttpResponseInfo::CONNECTION_INFO_UNKNOWN:
      return "";
    case net::HttpResponseInfo::CONNECTION_INFO_HTTP0_9:
    case net::HttpResponseInfo::CONNECTION_INFO_HTTP1_0:
    case net::HttpResponseInfo::CONNECTION_INFO_HTTP1_1:
      return ssl_info_populated ? "HTTPS" : "HTTP";
    case net::HttpResponseInfo::CONNECTION_INFO_DEPRECATED_SPDY2:
    case net::HttpResponseInfo::CONNECTION_INFO_DEPRECATED_SPDY3:
    case net::HttpResponseInfo::CONNECTION_INFO_DEPRECATED_HTTP2_14:
    case net::HttpResponseInfo::CONNECTION_INFO_DEPRECATED_HTTP2_15:
    case net::HttpResponseInfo::CONNECTION_INFO_HTTP2:
      return "SPDY";
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_UNKNOWN_VERSION:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_32:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_33:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_34:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_35:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_36:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_37:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_38:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_39:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_40:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_41:
    case net::HttpResponseInfo::CONNECTION_INFO_QUIC_42:
      return "QUIC";
    case net::HttpResponseInfo::NUM_OF_CONNECTION_INFOS:
      NOTREACHED();
      return "";
  }
  NOTREACHED();
  return "";
}

int GetNetErrorFromURLRequestStatus(const net::URLRequestStatus& status) {
  switch (status.status()) {
    case net::URLRequestStatus::SUCCESS:
      return net::OK;
    case net::URLRequestStatus::CANCELED:
      return net::ERR_ABORTED;
    case net::URLRequestStatus::FAILED:
      return status.error();
    default:
      NOTREACHED();
      return net::ERR_FAILED;
  }
}

void GetUploadResultFromResponseDetails(
    int net_error,
    int http_response_code,
    base::TimeDelta retry_after,
    DomainReliabilityUploader::UploadResult* result) {
  if (net_error == net::OK && http_response_code == 200) {
    result->status = DomainReliabilityUploader::UploadResult::SUCCESS;
    return;
  }

  if (net_error == net::OK &&
      http_response_code == 503 &&
      !retry_after.is_zero()) {
    result->status = DomainReliabilityUploader::UploadResult::RETRY_AFTER;
    result->retry_after = retry_after;
    return;
  }

  result->status = DomainReliabilityUploader::UploadResult::FAILURE;
  return;
}

// N.B. This uses a std::vector<std::unique_ptr<>> because that's what
// JSONValueConverter uses for repeated fields of any type, and Config uses
// JSONValueConverter to parse JSON configs.
GURL SanitizeURLForReport(
    const GURL& beacon_url,
    const GURL& collector_url,
    const std::vector<std::unique_ptr<std::string>>& path_prefixes) {
  if (CanReportFullBeaconURLToCollector(beacon_url, collector_url))
    return beacon_url.GetAsReferrer();

  std::string path = beacon_url.path();
  const std::string empty_path;
  const std::string* longest_path_prefix = &empty_path;
  for (const auto& path_prefix : path_prefixes) {
    if (path.substr(0, path_prefix->length()) == *path_prefix &&
        path_prefix->length() > longest_path_prefix->length()) {
      longest_path_prefix = path_prefix.get();
    }
  }

  GURL::Replacements replacements;
  replacements.ClearUsername();
  replacements.ClearPassword();
  replacements.SetPathStr(*longest_path_prefix);
  replacements.ClearQuery();
  replacements.ClearRef();
  return beacon_url.ReplaceComponents(replacements);
}

namespace {

class ActualTimer : public MockableTime::Timer {
 public:
  // Initialize base timer with retain_user_info and is_repeating false.
  ActualTimer() : base_timer_(false, false) {}

  ~ActualTimer() override {}

  // MockableTime::Timer implementation:
  void Start(const base::Location& posted_from,
             base::TimeDelta delay,
             const base::Closure& user_task) override {
    base_timer_.Start(posted_from, delay, user_task);
  }

  void Stop() override { base_timer_.Stop(); }

  bool IsRunning() override { return base_timer_.IsRunning(); }

 private:
  base::Timer base_timer_;
};

}  // namespace

MockableTime::Timer::~Timer() {}
MockableTime::Timer::Timer() {}

MockableTime::~MockableTime() {}
MockableTime::MockableTime() {}

ActualTime::ActualTime() {}
ActualTime::~ActualTime() {}

base::Time ActualTime::Now() { return base::Time::Now(); }
base::TimeTicks ActualTime::NowTicks() { return base::TimeTicks::Now(); }

std::unique_ptr<MockableTime::Timer> ActualTime::CreateTimer() {
  return std::unique_ptr<MockableTime::Timer>(new ActualTimer());
}

}  // namespace domain_reliability
