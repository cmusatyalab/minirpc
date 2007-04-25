/*** Generic types ***********************************************************/

typedef unsigned ParcelVer;
typedef opaque Hash<64>;
typedef opaque EncryptedKeyRecord<>;
typedef opaque EncryptedKeyRootRecord<>;
typedef unsigned Timestamp;  /* XXX */
typedef string String<>;
typedef opaque ChunkData<131072>;

enum ChunkPlane {
	PLANE_DISK = 0,
	PLANE_MEMORY = 1
};

enum Compression {
	COMPRESS_NONE = 0,
	COMPRESS_ZLIB = 1,
	COMPRESS_LZF = 2
};

enum Encryption {
	ENCRYPT_NONE_SHA1 = 0,
	ENCRYPT_BLOWFISH_SHA1 = 1,
	ENCRYPT_AES_SHA1 = 2
};

struct ChunkID {
	unsigned	cid;
	ChunkPlane	plane;
};

typedef ChunkID ChunkArray<>;

struct DateRange {
	Timestamp	start;
	Timestamp	stop;
};

struct KeyRecord {
	Hash		key;
	Compression	compress;
	Encryption	encrypt;
};

struct KeyRootRecord {
	Hash		key;
};

/** Define:	reply **/
enum Status {
	STATUS_OK,
	STATUS_CONTINUE,		/* authentication incomplete */
	STATUS_MESSAGE_UNKNOWN,
	STATUS_MESSAGE_TOO_LARGE,
	STATUS_NOT_SUPPORTED,
	STATUS_TLS_REQUIRED,
	STATUS_NOT_AUTHENTICATED,
	STATUS_AUTH_FAILED,
	STATUS_PARCEL_LOCKED,
	STATUS_NO_SUCH_PARCEL,
	STATUS_NO_SUCH_VERSION,
	STATUS_NO_SUCH_RECORD,
	STATUS_NO_DATA,
	STATUS_DATA_INCOMPLETE,	/* XXX redundant? */
	STATUS_QUOTA_EXCEEDED
};

/*** Startup and authentication messages *************************************/

enum SupportedProtocolVersions {
	VER_V0 = 0
};

/** Define:	request **/
/** Replies:	ClientHello **/
/** Initiators:	server **/
struct ServerHello {
	String		software;
	String		ver;
	String		*banner;
	unsigned	protocolSupportBitmap;
	bool		tlsSupported;
	bool		tlsRequired;
	String		authTypes;		/* space-separated */ /* XXX */
};

/** Define:	reply **/
struct ClientHello {
	String		software;
	String		ver;
	SupportedProtocolVersions	protocolChosen;
};

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client **/
/* StartTLS */

/** Define:	request **/
/** Replies:	AuthReply **/
/** Initiators:	client **/
struct Authenticate {
	String		method;
	ChunkData	data;
};

/** Define:	request **/
/** Replies:	AuthReply **/
/** Initiators:	client **/
typedef opaque AuthStep<>;

/** Define:	reply **/
struct AuthReply {
	Status		status;
	String		*message;
	ChunkData	data;
};

/* XXX should password changing be part of the protocol?  could provide a
   "get password change URL" message for user experience. */

/*** Parcel operations *******************************************************/

/* @force can be declined if an open connection currently exists which holds
   the lock? */
/** Define:	request **/
/** Replies:	NewCookie Status **/
/** Initiators:	client **/
struct AcquireLock {
	String		parcel;
	ParcelVer	ver;
	String		owner;
	String		*comment;
	unsigned	*cookie;
	bool		force;
};

/** Define:	reply **/
typedef unsigned NewCookie;

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client **/
struct ReleaseLock {
	String		parcel;
	unsigned	cookie;
};

/* On checkout, a temporary area for keyring updates is (logically) created
   and is based on the keyring version that was checked out.  Keyring updates
   may then be sent, and may supersede previous keyring updates.  Chunk uploads
   may be made against the keys in the temporary keyring.  Commit will not
   be allowed until every new key has the corresponding chunk data. */

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client **/
struct ParcelCommit {
	String		parcel;
	String		comment;
};

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client **/
struct AddParcel {
	String		parcel;
	String		*comment;
	unsigned	chunks;
	EncryptedKeyRootRecord root;
};

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client **/
struct UpdateParcel {
	String		parcel;
	String		*comment;
};

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client **/
struct UpdateKeyRoot {
	EncryptedKeyRootRecord root;
};

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client **/
struct RemoveParcel {
	String		parcel;
	ParcelVer	*ver;
};

/*** ListParcels *************************************************************/

/** Define:	request **/
/** Replies:	ListParcelsReply **/
/** Initiators:	client **/
struct ListParcels {
	String		*name;
	DateRange	*history;
};

struct ParcelVersionInfo {
	ParcelVer	ver;
	String		*comment;
	Timestamp	checkin;
	unsigned	chunks;		/* unique chunks */
};

struct ParcelLockInfo {
	ParcelVersionInfo ver;
	String		by;
	Timestamp	at;
	String		*comment;
};

struct ParcelInfo {
	String		name;
	String		*comment;
	ParcelVersionInfo	current;
	ParcelLockInfo	*acquired;
	ParcelVersionInfo	history<>;
};

/** Define:	reply **/
typedef ParcelInfo ListParcelsReply<>;

/*** Chunk operations ********************************************************/

enum ChunkLookupBy {
	BY_CHUNK,
	BY_TAG
};

union ChunkLookupKey switch (ChunkLookupBy key) {
case BY_CHUNK:
	ChunkID		chunk;
case BY_TAG:
	Hash		tag;
};

enum WantChunkInfo {
	WANT_CHUNKS = 0,
	WANT_KEY = 1,
	WANT_DATA = 2
};

/** Define:	request **/
/** Replies:	ChunkReply Status **/
/** Initiators:	client **/
struct ChunkRequest {
	ChunkLookupKey	by;
	unsigned	WantChunkInfoBits;
};

/** Define:	reply **/
struct ChunkReply {
	ChunkArray	*chunks;
	EncryptedKeyRecord *keyrec;
	ChunkData	*data;
};

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client **/
struct KeyUpdate {
	ChunkArray	chunks;
	Hash		tag;
	EncryptedKeyRecord keyrec;
};

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client **/
struct ChunkUpload {
	Hash		tag;
	ChunkData	data;
};

/** Define:	request **/
/** Replies:	NeedTags Status **/
/** Initiators:	client **/
struct GetMissingData {
	String		parcel;
	unsigned	startIndex;	/* message size limit applies */
	unsigned	*stopIndex;
};

/** Define:	reply **/
typedef Hash NeedTags<>;

/*** Miscellaneous messages **************************************************/

/** Define:	request **/
/** Replies:	Status **/
/** Initiators:	client server **/
/* Ping */

/*** Top-level message *******************************************************/

enum MessageType {
	MTYPE_Status,
	MTYPE_Ping,
	
	MTYPE_ServerHello,
	MTYPE_ClientHello,
	
	MTYPE_StartTLS,
	MTYPE_Authenticate,
	MTYPE_AuthStep,
	MTYPE_AuthReply,
	
	MTYPE_AcquireLock,
	MTYPE_NewCookie,
	MTYPE_ReleaseLock,
	MTYPE_AddParcel,
	MTYPE_UpdateParcel,
	MTYPE_UpdateKeyRoot,
	MTYPE_RemoveParcel,
	MTYPE_ParcelCommit,
	
	MTYPE_ListParcels,
	MTYPE_ListParcelsReply,
	
	MTYPE_ChunkRequest,
	MTYPE_ChunkReply,
	MTYPE_KeyUpdate,
	MTYPE_ChunkUpload,
	MTYPE_GetMissingData,
	MTYPE_NeedTags
};

/** Define:	parent **/
union MessageBody switch (MessageType type) {
case MTYPE_Status:
	Status		status;
case MTYPE_Ping:
	void;
	
case MTYPE_ServerHello:
	ServerHello	serverhello;
case MTYPE_ClientHello:
	ClientHello	clienthello;
	
case MTYPE_StartTLS:
	void;
case MTYPE_Authenticate:
	Authenticate	authenticate;
case MTYPE_AuthStep:
	AuthStep	authstep;
case MTYPE_AuthReply:
	AuthReply	authreply;
	
case MTYPE_AcquireLock:
	AcquireLock	acquirelock;
case MTYPE_NewCookie:
	NewCookie	newcookie;
case MTYPE_ReleaseLock:
	ReleaseLock	releaselock;
case MTYPE_AddParcel:
	AddParcel	addparcel;
case MTYPE_UpdateParcel:
	UpdateParcel	updateparcel;
case MTYPE_UpdateKeyRoot:
	UpdateKeyRoot	updatekeyroot;
case MTYPE_RemoveParcel:
	RemoveParcel	removeparcel;
case MTYPE_ParcelCommit:
	ParcelCommit	parcelcommit;
	
case MTYPE_ListParcels:
	ListParcels	listparcels;
case MTYPE_ListParcelsReply:
	ListParcelsReply listparcelsreply;
	
case MTYPE_ChunkRequest:
	ChunkRequest	chunkrequest;
case MTYPE_ChunkReply:
	ChunkReply	chunkreply;
case MTYPE_KeyUpdate:
	KeyUpdate	keyupdate;
case MTYPE_ChunkUpload:
	ChunkUpload	chunkupload;
case MTYPE_GetMissingData:
	GetMissingData	getmissingdata;
case MTYPE_NeedTags:
	NeedTags	needtags;
};

struct ISRMessage {
	unsigned	sequence;
	bool		isReply;
	MessageBody	body;
};
