// SPDX-License-Identifier: AGPL-1.0-only
// Copyright (C) 2018 Ludvig Strigeus <info@tunsafe.com>. All Rights Reserved.
#pragma once

#include "tunsafe_types.h"
#include "netapi.h"
#include "ipzip2/ipzip2.h"
#include "tunsafe_config.h"
#include "tunsafe_threading.h"
#include "ip_to_peer_map.h"
#include <vector>
#include <unordered_map>
#include <atomic>

// Threading macros that enable locks only in MT builds
#if WITH_WG_THREADING
#define WG_SCOPED_LOCK(name) AutoLock scoped_lock(&name)
#define WG_ACQUIRE_LOCK(name) name.Acquire()
#define WG_RELEASE_LOCK(name) name.Release()
#define WG_DECLARE_LOCK(name) Mutex name;
#define WG_DECLARE_RWLOCK(name) ReaderWriterLock name;
#define WG_ACQUIRE_RWLOCK_SHARED(name) name.AcquireShared()
#define WG_RELEASE_RWLOCK_SHARED(name) name.ReleaseShared()
#define WG_ACQUIRE_RWLOCK_EXCLUSIVE(name) name.AcquireExclusive()
#define WG_RELEASE_RWLOCK_EXCLUSIVE(name) name.ReleaseExclusive()
#define WG_SCOPED_RWLOCK_SHARED(name) ScopedLockShared scoped_lock(&name)
#define WG_SCOPED_RWLOCK_EXCLUSIVE(name) ScopedLockExclusive scoped_lock(&name)
#else  // WITH_WG_THREADING
#define WG_SCOPED_LOCK(name) 
#define WG_ACQUIRE_LOCK(name) 
#define WG_RELEASE_LOCK(name)
#define WG_DECLARE_LOCK(name)
#define WG_DECLARE_RWLOCK(name)
#define WG_ACQUIRE_RWLOCK_SHARED(name)
#define WG_RELEASE_RWLOCK_SHARED(name)
#define WG_ACQUIRE_RWLOCK_EXCLUSIVE(name)
#define WG_RELEASE_RWLOCK_EXCLUSIVE(name)
#define WG_SCOPED_RWLOCK_SHARED(name)
#define WG_SCOPED_RWLOCK_EXCLUSIVE(name)
#endif  // WITH_WG_THREADING

enum ProtocolTimeouts {
  COOKIE_SECRET_MAX_AGE_MS = 120000,
  COOKIE_SECRET_LATENCY_MS = 5000,
  REKEY_TIMEOUT_MS = 5000,
  KEEPALIVE_TIMEOUT_MS = 10000,
  REKEY_AFTER_TIME_MS = 120000,
  REJECT_AFTER_TIME_MS = 180000,
  PERSISTENT_KEEPALIVE_MS = 25000,
  MIN_HANDSHAKE_INTERVAL_MS = 20,

  MAX_SIZE_OF_HANDSHAKE_EXTENSION = 1024,
};

enum ProtocolLimits {
  REJECT_AFTER_MESSAGES = UINT64_MAX - 2048,
  REKEY_AFTER_MESSAGES = UINT64_MAX - 0xffff,

  MAX_HANDSHAKE_ATTEMPTS = 20,
  MAX_QUEUED_PACKETS_PER_PEER = 128,
  MESSAGE_MINIMUM_SIZE = 16,
};

enum MessageType {
  MESSAGE_HANDSHAKE_INITIATION = 1,
  MESSAGE_HANDSHAKE_RESPONSE = 2,
  MESSAGE_HANDSHAKE_COOKIE = 3,
  MESSAGE_DATA = 4,
};

enum MessageFieldSizes {
  WG_COOKIE_LEN = 16,
  WG_COOKIE_NONCE_LEN = 24,
  WG_PUBLIC_KEY_LEN = 32,
  WG_HASH_LEN = 32,
  WG_SYMMETRIC_KEY_LEN = 32,
  WG_MAC_LEN = 16,
  WG_TIMESTAMP_LEN = 12,
  WG_SIPHASH_KEY_LEN = 16,
};

enum {
  WG_SHORT_HEADER_BIT = 0x80,
  WG_SHORT_HEADER_KEY_ID_MASK = 0x60,
  WG_SHORT_HEADER_KEY_ID = 0x20,
  WG_SHORT_HEADER_ACK = 0x10,
  WG_SHORT_HEADER_TYPE_MASK = 0x0F,
  WG_SHORT_HEADER_CTR1 = 0x00,
  WG_SHORT_HEADER_CTR2 = 0x01,
  WG_SHORT_HEADER_CTR4 = 0x02,

  WG_ACK_HEADER_COUNTER_MASK = 0x0C,
  WG_ACK_HEADER_COUNTER_NONE = 0x00,
  WG_ACK_HEADER_COUNTER_2 = 0x04,
  WG_ACK_HEADER_COUNTER_4 = 0x08,
  WG_ACK_HEADER_COUNTER_6 = 0x0C,

  WG_ACK_HEADER_KEY_MASK = 3,
};


struct MessageMacs {
  uint8 mac1[WG_COOKIE_LEN];
  uint8 mac2[WG_COOKIE_LEN];
};
STATIC_ASSERT(sizeof(MessageMacs) == 32, MessageMacs_wrong_size);

struct MessageHandshakeInitiation {
  uint32 type;
  uint32 sender_key_id;
  uint8 ephemeral[WG_PUBLIC_KEY_LEN];
  uint8 static_enc[WG_PUBLIC_KEY_LEN + WG_MAC_LEN];
  uint8 timestamp_enc[WG_TIMESTAMP_LEN + WG_MAC_LEN];
  MessageMacs mac;
};
STATIC_ASSERT(sizeof(MessageHandshakeInitiation) == 148, MessageHandshakeInitiation_wrong_size);

// Format of variable length payload.
// 1 byte type
// 1 byte length
// <payload>



struct MessageHandshakeResponse {
  uint32 type;
  uint32 sender_key_id;
  uint32 receiver_key_id;
  uint8 ephemeral[WG_PUBLIC_KEY_LEN];
  uint8 empty_enc[WG_MAC_LEN];
  MessageMacs mac;
};
STATIC_ASSERT(sizeof(MessageHandshakeResponse) == 92, MessageHandshakeResponse_wrong_size);

struct MessageHandshakeCookie {
  uint32 type;
  uint32 receiver_key_id;
  uint8 nonce[WG_COOKIE_NONCE_LEN];
  uint8 cookie_enc[WG_COOKIE_LEN + WG_MAC_LEN];
};
STATIC_ASSERT(sizeof(MessageHandshakeCookie) == 64, MessageHandshakeCookie_wrong_size);

struct MessageData {
  uint32 type;
  uint32 receiver_id;
  uint64 counter;
};
STATIC_ASSERT(sizeof(MessageData) == 16, MessageData_wrong_size);

enum {
  EXT_PACKET_COMPRESSION = 0x15,
  EXT_PACKET_COMPRESSION_VER = 0x01,

  EXT_BOOLEAN_FEATURES = 0x16,

  EXT_CIPHER_SUITES = 0x18,
  EXT_CIPHER_SUITES_PRIO = 0x19,

  // The standard wireguard chacha
  EXT_CIPHER_SUITE_CHACHA20POLY1305 = 0x00,
  // AES GCM 128 bit
  EXT_CIPHER_SUITE_AES128_GCM = 0x01,
  // AES GCM 256 bit
  EXT_CIPHER_SUITE_AES256_GCM = 0x02,
  // Same as CHACHA20POLY1305 but without the encryption step
  EXT_CIPHER_SUITE_NONE_POLY1305 = 0x03,

  EXT_CIPHER_SUITE_COUNT = 4,

};

enum {
  WG_FEATURES_COUNT = 6,
  WG_FEATURE_ID_SHORT_HEADER = 0,    // Supports short headers
  WG_FEATURE_ID_SHORT_MAC = 1,       // Supports 8-byte MAC
  WG_FEATURE_ID_IPZIP = 2,           // Using ipzip
  WG_FEATURE_ID_SKIP_KEYID_IN = 4,   // Skip keyid for incoming packets
  WG_FEATURE_ID_SKIP_KEYID_OUT = 5,  // Skip keyid for outgoing packets
};

enum {
  WG_BOOLEAN_FEATURE_OFF = 0x0,
  WG_BOOLEAN_FEATURE_SUPPORTS = 0x1,
  WG_BOOLEAN_FEATURE_WANTS = 0x2,
  WG_BOOLEAN_FEATURE_ENFORCES = 0x3,
};

struct WgPacketCompressionVer01 {
  uint16 version;      // Packet compressor version
  uint8 ttl;           // Guessed TTL
  uint8 flags;         // Subnet length and packet direction
  uint8 ipv4_addr[4];  // IPV4 address of endpoint
  uint8 ipv6_addr[16]; // IPV6 address of endpoint
};
STATIC_ASSERT(sizeof(WgPacketCompressionVer01) == 24, WgPacketCompressionVer01_wrong_size);


struct WgKeypair;
class WgPeer;


class WgRateLimit {
public:
  WgRateLimit();

  struct RateLimitResult {
    uint8 *value_ptr;
    uint8 new_value;
    uint8 is_ok;

    bool is_rate_limited() { return !is_ok; }
    bool is_first_ip() { return new_value == 1; }
  };

  RateLimitResult CheckRateLimit(uint64 ip);

  void CommitResult(const RateLimitResult &rr) { *rr.value_ptr = rr.new_value; if (used_rate_limit_++ == TOTAL_PACKETS_PER_SEC) packets_per_sec_ = (packets_per_sec_ + 1) >> 1; }

  void Periodic(uint32 s[5]);

  bool is_used() { return used_rate_limit_ != 0 || packets_per_sec_ != PACKETS_PER_SEC; }
private:
  uint8 *bin1_, *bin2_;
  uint32 rand_, rand_xor_;
  uint32 packets_per_sec_, used_rate_limit_;
  uint64 key1_[2], key2_[2];
  enum {
    BINSIZE = 4096,
    PACKETS_PER_SEC = 25,
    PACKET_ACCUM = 100,
    TOTAL_PACKETS_PER_SEC = 25000,
  };
  uint8 bins_[2][BINSIZE];
};

struct WgAddrEntry {
  // The id of the addr entry, so we can delete ourselves
  uint64 addr_entry_id;

  // Ensure there's at least 1 minute between we allow registering
  // a new key in this table. This means that each key will have
  // a life time of at least 3 minutes.
  uint64 time_of_last_insertion;

  // This entry gets erased when there's no longer any key pointing at it.
  uint8 ref_count;

  // Index of the next slot 0-2 where we'll insert the next key.
  uint8 next_slot;

  // The three keys.
  WgKeypair *keys[3];

  WgAddrEntry(uint64 addr_entry_id) : addr_entry_id(addr_entry_id), ref_count(0), next_slot(0) {
    keys[0] = keys[1] = keys[2] = NULL;
    time_of_last_insertion = 0x123456789123456;
  }
};

struct ScramblerSiphashKeys {
  uint64 keys[4];
};
 
class WgDevice {
  friend class WgPeer;
  friend class WireguardProcessor;
public:

  // Can be used to customize the behavior of WgDevice
  class Delegate {
  public:
    // This is called from the main thread whenever a public key was not found in the WgDevice,
    // return true to try again or false to fail. The packet can be copied and saved
    // to resume a handshake later on.
    virtual bool HandleUnknownPeerId(uint8 public_key[WG_PUBLIC_KEY_LEN], Packet *packet) = 0;
  };

  WgDevice();
  ~WgDevice();

  // Initialize with the private key, precompute all internal keys etc.
  void Initialize(const uint8 private_key[WG_PUBLIC_KEY_LEN]);

  // Create a new peer
  WgPeer *AddPeer();

  // Setup header obfuscation
  void SetHeaderObfuscation(const char *key);

  // Check whether Mac1 appears to be valid
  bool CheckCookieMac1(Packet *packet);

  // Check whether Mac2 appears to be valid, this also uses the remote ip address
  bool CheckCookieMac2(Packet *packet);

  void CreateCookieMessage(MessageHandshakeCookie *dst, Packet *packet, uint32 remote_key_id);
  void UpdateKeypairAddrEntry_Locked(uint64 addr_id, WgKeypair *keypair);
  void SecondLoop(uint64 now);

  IpToPeerMap &ip_to_peer_map() { return ip_to_peer_map_; }
  WgPeer *first_peer() { return peers_; }
  const uint8 *public_key() const { return s_pub_; }
  WgRateLimit *rate_limiter() { return &rate_limiter_; }
  std::unordered_map<uint64, WgAddrEntry*> &addr_entry_map() { return addr_entry_lookup_; }
  WgPacketCompressionVer01 *compression_header() { return &compression_header_; }

  bool IsMainThread() { return CurrentThreadIdEquals(main_thread_id_); }
  void SetCurrentThreadAsMainThread() { main_thread_id_ = GetCurrentThreadId(); }

  void SetDelegate(Delegate *del) { delegate_ = del; }
private:
  std::pair<WgPeer*, WgKeypair*> *LookupPeerInKeyIdLookup(uint32 key_id);
  WgKeypair *LookupKeypairByKeyId(uint32 key_id);
  WgKeypair *LookupKeypairInAddrEntryMap(uint64 addr, uint32 slot);
  // Return the peer matching the |public_key| or NULL
  WgPeer *GetPeerFromPublicKey(uint8 public_key[WG_PUBLIC_KEY_LEN]);
  // Create a cookie by inspecting the source address of the |packet|
  void MakeCookie(uint8 cookie[WG_COOKIE_LEN], Packet *packet);
  // Insert a new entry in |key_id_lookup_|
  uint32 InsertInKeyIdLookup(WgPeer *peer, WgKeypair *kp);
  // Get a random number
  uint32 GetRandomNumber();

  void EraseKeypairAddrEntry_Locked(WgKeypair *kp);

  // Maps IP addresses to peers
  IpToPeerMap ip_to_peer_map_;

  // This lock protects |ip_to_peer_map_|.
  WG_DECLARE_RWLOCK(ip_to_peer_map_lock_);
   
  // For enumerating all peers
  WgPeer *peers_;

  // For hooking
  Delegate *delegate_;

  // Lock that protects key_id_lookup_
  WG_DECLARE_RWLOCK(key_id_lookup_lock_);
  // Mapping from key-id to either an active keypair (if keypair is non-NULL),
  // or to a handshake.
  std::unordered_map<uint32, std::pair<WgPeer*, WgKeypair*> > key_id_lookup_;

  // Mapping from IPV4 IP/PORT to WgPeer*, so we can find the peer when a key id is
  // not explicitly included.
  std::unordered_map<uint64, WgAddrEntry*> addr_entry_lookup_;
  WG_DECLARE_RWLOCK(addr_entry_lookup_lock_);

  // Counter for generating new indices in |keypair_lookup_|
  uint8 next_rng_slot_;

  // Whether packet obfuscation is enabled
  bool header_obfuscation_;

  ThreadId main_thread_id_;

  uint64 low_resolution_timestamp_;

  uint64 cookie_secret_timestamp_;
  uint8 cookie_secret_[WG_HASH_LEN];
  uint8 s_priv_[WG_PUBLIC_KEY_LEN];
  uint8 s_pub_[WG_PUBLIC_KEY_LEN];

  // Siphash keys for packet scrambling
  ScramblerSiphashKeys header_obfuscation_key_;

  uint8 precomputed_cookie_key_[WG_SYMMETRIC_KEY_LEN];
  uint8 precomputed_mac1_key_[WG_SYMMETRIC_KEY_LEN];

  uint64 random_number_input_[WG_HASH_LEN / 8 + 1];
  uint32 random_number_output_[WG_HASH_LEN / 4];

  WgRateLimit rate_limiter_;

  WgPacketCompressionVer01 compression_header_;

  // For defering deletes until all worker threads are guaranteed not to use an object.
  MultithreadedDelayedDelete delayed_delete_;
};

// State for peer
class WgPeer {
  friend class WgDevice;
  friend class WireguardProcessor;
  friend bool WgKeypairParseExtendedHandshake(WgKeypair *keypair, const uint8 *data, size_t data_size);
  friend void WgKeypairSetupCompressionExtension(WgKeypair *keypair, const WgPacketCompressionVer01 *remotec);
public:
  explicit WgPeer(WgDevice *dev);
  ~WgPeer();

  void Initialize(const uint8 spub[WG_PUBLIC_KEY_LEN], const uint8 preshared_key[WG_SYMMETRIC_KEY_LEN]);

  void SetPersistentKeepalive(int persistent_keepalive_secs);
  void SetEndpoint(const IpAddr &sin);
  void SetAllowMulticast(bool allow);

  void SetFeature(int feature, uint8 value);
  bool AddCipher(int cipher);
  void SetCipherPrio(bool prio) { cipher_prio_ = prio; }
  bool AddIp(const WgCidrAddr &cidr_addr);

  static WgPeer *ParseMessageHandshakeInitiation(WgDevice *dev, Packet *packet);
  static WgPeer *ParseMessageHandshakeResponse(WgDevice *dev, const Packet *packet);
  static void ParseMessageHandshakeCookie(WgDevice *dev, const MessageHandshakeCookie *src);
  void CreateMessageHandshakeInitiation(Packet *packet);
  bool CheckSwitchToNextKey_Locked(WgKeypair *keypair);
  void ClearKeys_Locked();
  void ClearHandshake_Locked();
  void ClearPacketQueue_Locked();
  bool CheckHandshakeRateLimit();

  // Timer notifications
  void OnDataSent();
  void OnKeepaliveSent();
  void OnDataReceived();
  void OnKeepaliveReceived();
  void OnHandshakeInitSent();
  void OnHandshakeAuthComplete();
  void OnHandshakeFullyComplete();

  enum {
    ACTION_SEND_KEEPALIVE = 1,
    ACTION_SEND_HANDSHAKE = 2,
  };
  uint32 CheckTimeouts(uint64 now);

  void AddPacketToPeerQueue(Packet *packet);

#if WITH_WG_THREADING
  bool IsPeerLocked() { return mutex_.IsLocked(); }
#else  // WITH_WG_THREADING
  bool IsPeerLocked() { return true; }
#endif  // WITH_WG_THREADING

private:
  static WgKeypair *CreateNewKeypair(bool is_initiator, const uint8 key[WG_HASH_LEN], uint32 send_key_id, const uint8 *extfield, size_t extfield_size);
  void WriteMacToPacket(const uint8 *data, MessageMacs *mac);
  void DeleteKeypair(WgKeypair **kp);
  void CheckAndUpdateTimeOfNextKeyEvent(uint64 now);
  static void CopyEndpointToPeer_Locked(WgKeypair *keypair, const IpAddr *addr);
  size_t WriteHandshakeExtension(uint8 *dst, WgKeypair *keypair);
  void InsertKeypairInPeer_Locked(WgKeypair *keypair);

  WgDevice *dev_;
  WgPeer *next_peer_;

  // Keypairs, |curr_keypair_| is the used one, the other ones are
  // the old ones and the next one.
  WgKeypair *curr_keypair_, *prev_keypair_, *next_keypair_;

  // Protects shared variables of the WgPeer
  WG_DECLARE_LOCK(mutex_);

  // Timestamp when the next key related event is going to occur.
  uint64 time_of_next_key_event_;

  // For timer management
  uint32 timers_;
  uint32 timer_value_[5];

  // Holds the entry into the key id table during handshake - mt only.
  uint32 local_key_id_during_hs_;

  // Address of peer
  IpAddr endpoint_;

  enum {
    kMainThreadScheduled_ScheduleHandshake = 1,
  };
  std::atomic<uint32> main_thread_scheduled_;
  WgPeer *main_thread_scheduled_next_;

  // The broadcast address of the IPv4 network, used to block broadcast traffic
  // from being sent out over the VPN link.
  uint32 ipv4_broadcast_addr_;

  // Whether the tunsafe specific handshake extensions are supported
  bool supports_handshake_extensions_;

  // Whether any data was sent since the keepalive timer was set
  bool pending_keepalive_;

  // Whether to change the endpoint on incoming packets.
  bool allow_endpoint_change_;

  // Whether we've sent a mac to the peer so we may expect a cookie reply back.
  bool expect_cookie_reply_;

  // Whether we want to route incoming multicast/broadcast traffic to this peer.
  bool allow_multicast_through_peer_;

  // Whether |mac2_cookie_| is valid.
  bool has_mac2_cookie_;

  // Number of handshakes made so far, when this gets too high we stop connecting.
  uint8 handshake_attempts_;

  // Which features are enabled for this peer?
  uint8 features_[WG_FEATURES_COUNT];

  // Queue of packets that will get sent once handshake finishes
  uint8 num_queued_packets_;
  Packet *first_queued_packet_, **last_queued_packet_ptr_;
  
  // For statistics
  uint64 last_handshake_init_timestamp_;
  uint64 last_complete_handskake_timestamp_;

  // Timestamp to detect flooding of handshakes
  uint64 last_handshake_init_recv_timestamp_;  // main thread only

  // Number of handshake attempts since last successful handshake
  uint32 total_handshake_attempts_;

  // For dynamic ciphers, holds the list of supported ciphers.
  enum { MAX_CIPHERS = 4 };
  uint8 cipher_prio_;
  uint8 num_ciphers_;
  uint8 ciphers_[MAX_CIPHERS];
  
  // Handshake state that gets setup in |CreateMessageHandshakeInitiation| and used in
  // the response.
  struct HandshakeState {
    // Hash
    uint8 hi[WG_HASH_LEN];
    // Chaining key
    uint8 ci[WG_HASH_LEN];
    // Private ephemeral
    uint8 e_priv[WG_PUBLIC_KEY_LEN];
  };
  HandshakeState hs_;
  // Remote's static public key - init only.
  uint8 s_remote_[WG_PUBLIC_KEY_LEN];
  // Remote's preshared key - init only.
  uint8 preshared_key_[WG_SYMMETRIC_KEY_LEN];
  // Precomputed DH(spriv_local, spub_remote) - init only.
  uint8 s_priv_pub_[WG_PUBLIC_KEY_LEN];
  // The most recent seen timestamp, only accept higher timestamps - mt only.
  uint8 last_timestamp_[WG_TIMESTAMP_LEN]; 
  // Precomputed key for decrypting cookies from the peer - init only.
  uint8 precomputed_cookie_key_[WG_SYMMETRIC_KEY_LEN];
  // Precomputed key for sending MACs to the peer - init only.
  uint8 precomputed_mac1_key_[WG_SYMMETRIC_KEY_LEN];
  // The last mac value sent, required to make cookies - mt only.
  uint8 sent_mac1_[WG_COOKIE_LEN];
  // The mac2 cookie that gets appended to outgoing packets
  uint8 mac2_cookie_[WG_COOKIE_LEN];
  // The timestamp of the mac2 cookie
  uint64 mac2_cookie_timestamp_;
  int persistent_keepalive_ms_;

  // Allowed ips
  std::vector<WgCidrAddr> allowed_ips_;
};

// RFC6479 - IPsec Anti-Replay Algorithm without Bit Shifting
class ReplayDetector {
public:
  ReplayDetector();
  ~ReplayDetector();

  bool CheckReplay(uint64 other);
  enum {
    BITS_PER_ENTRY = 32,
    WINDOW_SIZE = 2048 - BITS_PER_ENTRY,
    BITMAP_SIZE = WINDOW_SIZE / BITS_PER_ENTRY + 1,
    BITMAP_MASK = BITMAP_SIZE - 1,
  };

  const uint64 expected_seq_nr() const { return expected_seq_nr_; }

private:
  std::atomic<uint64> expected_seq_nr_;
  uint32 bitmap_[BITMAP_SIZE];
};

struct AesGcm128StaticContext;

struct WgKeypair {
  WgPeer *peer;

  // If the key has an addr entry mapping,
  // then this points at it.
  WgAddrEntry *addr_entry;
  // The slot in the addr entry where the key is registered.
  uint8 addr_entry_slot;

  enum {
    KEY_INVALID = 0,
    KEY_VALID = 1,
    KEY_WANT_REFRESH = 2,
    KEY_DID_REFRESH = 3,
  };
  // True if i'm the initiator of the key exchange
  bool is_initiator;

  // True if we saved the peer's address in our table recently,
  // avoids doing it too much
  bool did_attempt_remember_ip_port;

  // Which features are enabled
  bool enabled_features[WG_FEATURES_COUNT];

  // True if we want to notify the sender about that it can use a short key.
  uint8 broadcast_short_key;

  // Index of the short key index that we can use for outgoing packets.
  uint8 can_use_short_key_for_outgoing;

  // Whether the key is valid or needs refresh for receives
  uint8 recv_key_state;
  // Whether the key is valid or needs refresh for sends
  uint8 send_key_state;

  // Length of authentication tag
  uint8 auth_tag_length;

  // Cipher suite
  uint8 cipher_suite;
  
  // Used so we know when to send out ack packets.
  uint32 incoming_packet_count;

  // Id of the key in my map. (MainThread)
  uint32 local_key_id;
  // Id of the key in their map
  uint32 remote_key_id;
  // The timestamp of when the key was created, to be able to expire it
  uint64 key_timestamp;
  // The highest acked send_ctr value
  uint64 send_ctr_acked;
  // Counter value for chacha20 for outgoing packets
  uint64 send_ctr;
  // The key used for chacha20 encryption
  uint8 send_key[WG_SYMMETRIC_KEY_LEN];
  // The key used for chacha20 decryption
  uint8 recv_key[WG_SYMMETRIC_KEY_LEN];

  // Used when less than 16-byte mac is enabled to hash the hmac into 64 bits.
  uint64 compress_mac_keys[2][2];

  AesGcm128StaticContext *aes_gcm128_context_;

  // -- all up to this point is initialized to zero
  // For replay detection of incoming packets
  ReplayDetector replay_detector;

#if WITH_HANDSHAKE_EXT
  // State for packet compressor
  IpzipState ipzip_state_;
#endif  // WITH_HANDSHAKE_EXT
};

void WgKeypairEncryptPayload(uint8 *dst, const size_t src_len,
    const uint8 *ad, const size_t ad_len,
    const uint64 nonce, WgKeypair *keypair);

bool WgKeypairDecryptPayload(uint8 *dst, const size_t src_len,
    const uint8 *ad, const size_t ad_len,
    const uint64 nonce, WgKeypair *keypair);

bool WgKeypairParseExtendedHandshake(WgKeypair *keypair, const uint8 *data, size_t data_size);

