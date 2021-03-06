#include"lib.h"
struct nm_desc *lib_netmap_desc;
struct netmap_if *nifp;
struct netmap_ring *send_ring, *receive_ring;
struct nmreq nmr;
struct pollfd fds;
int fd, length;
int do_abort = 1;
const char *src_ip = "169.254.18.80";
const char *src_ip1;// = "169.254.18.80";
const char *src_mac = "00:aa:bb:cc:dd:04";
struct ether_addr *src_byte;
char* backend_mac_pool[2] = {"00:aa:bb:cc:dd:03", "00:aa:bb:cc:dd:06"};
unordered_map<int, fn> funct_ptr;
unordered_map<int, string> conn_map;
int map_index=0;
//data store part
boost::simple_segregated_storage<std::size_t> storageds;  //memory pool for data store
std::vector<char> mp_ds(64*131072);  //assuming value size 64 TODO
unordered_map<int, void*> ds_map1;   //data store if option is local //TODO make it general using boost
unordered_map<void*, int>local_list; //local list of addr for clearing cache..local dnt remove
unordered_map<int, void*>cache_list; //cache list of addr for clearing cache..cache remove
unordered_map<void*, int>cache_void_list; //cache list of addr for clearing cache..cache remove
unordered_map<void*, int>reqptr_list;  //list of addr pointed in req object needed for clearing cache..pointed dnt remove
mutex mct,eparr,sock_c,f_ptr_lock,mp_lock,ds_lock,ds_conn_lock;
//this were per core variables
unordered_map<int, void*> mem_ptr;
unordered_map<int, int> client_list;
std::unordered_map<int,int>::const_iterator got;
boost::simple_segregated_storage<std::size_t> storage1; 
	boost::simple_segregated_storage<std::size_t> storage2;
	boost::simple_segregated_storage<std::size_t> storage3;
	boost::simple_segregated_storage<std::size_t> storage4;
	int memory_size[4];
//
int ds_size = 0; //to keep count. If exceeds threshold clear
int ds_threshold = 131072, ds_sizing=1;
int ds_portno[4] = {7000,7001,7002,7003}; 
/*
 * Convert an ASCII representation of an ethernet address to
 * binary form.
 */
/*
This function finds the request block size in power of 2, closest to size specified by the user 
new name: initReqPool
*/
void initRequest(int msize[], int m_tot){  //size of chunks for request pool and total number of sizes sizeof(msize[])
	int p = 1,i,j;
	 cout<<"reached here"<<endl;
	int temp_memory_size[4];
	if(m_tot>4){
		cout<<"Only 4 pools allowed"<<endl;
		return;  //TODO error handling
	}	
        for(i=0;i<m_tot;i++){
		p=1;
		temp_memory_size[i]=0;
		if (msize[i] && !(msize[i] & (msize[i] - 1))){
			temp_memory_size[i] = msize[i];
			continue;
		}
		while (p < msize[i]) 
			p <<= 1;
	
			temp_memory_size[i] = p;
	}
	cout<<"MEMORY_size is "<<temp_memory_size[0]<<endl; 
	for(i=0;i<MAX_THREADS;i++){
		for(j=0;j<m_tot;j++){
			memory_size[j] = temp_memory_size[j];	
		}
	}

}
/* Free memory from data store pool when threshold reached. remove cached entry without the dne bit set*/
void free_ds_pool(){
	std::unordered_map<void*,int>::const_iterator gotds;
	for ( auto it = cache_void_list.begin(); it != cache_void_list.end(); ++it ){
		gotds = reqptr_list.find(it->first);
		if(gotds == reqptr_list.end()){
			cache_list.erase(it->second);
			ds_map1.erase(it->second);
			storageds.free(it->first);
		}
	}
	cache_void_list.clear();
	ds_size = 0;
}
struct ether_addr *ether_aton_dst(const char *a)
{
    int i;
    static struct ether_addr o;
    unsigned int o0, o1, o2, o3, o4, o5;

    i = sscanf(a, "%x:%x:%x:%x:%x:%x", &o0, &o1, &o2, &o3, &o4, &o5);

    if (i != 6)
        return (NULL);

    o.ether_addr_octet[0]=o0;
    o.ether_addr_octet[1]=o1;
    o.ether_addr_octet[2]=o2;
    o.ether_addr_octet[3]=o3;
    o.ether_addr_octet[4]=o4;
    o.ether_addr_octet[5]=o5;

    return ((struct ether_addr *)&o);
}

struct ether_addr *ether_aton_src(const char *a)
{
    int i;
    static struct ether_addr q;
    unsigned int o0, o1, o2, o3, o4, o5;

    i = sscanf(a, "%x:%x:%x:%x:%x:%x", &o0, &o1, &o2, &o3, &o4, &o5);

    if (i != 6)
        return (NULL);

    q.ether_addr_octet[0]=o0;
    q.ether_addr_octet[1]=o1;
    q.ether_addr_octet[2]=o2;
    q.ether_addr_octet[3]=o3;
    q.ether_addr_octet[4]=o4;
    q.ether_addr_octet[5]=o5;

    return ((struct ether_addr *)&q);
}
/*
registers a callback with an event on the interface
*/
void registerCallback(int connID, int id, string event, void callbackFnPtr(int, int,  void*, char*))
{
    if(id != -1)
        funct_ptr[connID] = callbackFnPtr;
    else
        funct_ptr[connID] = callbackFnPtr;

}
/* Define a struct for ARP header */
typedef struct _arp_hdr arp_hdr;
struct _arp_hdr {
  uint16_t htype;
  uint16_t ptype;
  uint8_t hlen;
  uint8_t plen;
  uint16_t opcode;
  uint8_t sender_mac[6];
  uint32_t sender_ip;
  uint8_t target_mac[6];
  uint32_t target_ip;
} __attribute__((__packed__));

/* ARP packet */
struct arp_pkt {
    struct ether_header eh;
    arp_hdr ah;
} __attribute__((__packed__));

struct arp_cache_entry {
    uint32_t ip;
    struct ether_addr mac;
};

static struct arp_cache_entry arp_cache[ARP_CACHE_LEN];
void insert_arp_cache(uint32_t ip, struct ether_addr mac) {
    int i;
    struct arp_cache_entry *entry;
    char ip_str[INET_ADDRSTRLEN];
    for(i = 0; i < ARP_CACHE_LEN; i++) {
        entry = &arp_cache[i];
        if (entry->ip == ip) {
            // // printf("arp entry already exists\n");
            return;
        }
        if (entry->ip == 0) {
            entry->ip = ip;
            entry->mac = mac;
            // // printf("arp entry created\n");
            return;

        }
    }
}
/* 
 * Change the destination mac field with ether_addr from given eth header
 */

void change_dst_mac(struct ether_header **ethh, struct ether_addr *p) {
  (*ethh)->ether_dhost[0] = p->ether_addr_octet[0];
  (*ethh)->ether_dhost[1] = p->ether_addr_octet[1];
  (*ethh)->ether_dhost[2] = p->ether_addr_octet[2];
  (*ethh)->ether_dhost[3] = p->ether_addr_octet[3];
  (*ethh)->ether_dhost[4] = p->ether_addr_octet[4];
  (*ethh)->ether_dhost[5] = p->ether_addr_octet[5];
}

/* 
 * Change the source mac field with ether_addr from given eth header
 */

void change_src_mac(struct ether_header **ethh, struct ether_addr *p) {
  (*ethh)->ether_shost[0] = p->ether_addr_octet[0];
  (*ethh)->ether_shost[1] = p->ether_addr_octet[1];
  (*ethh)->ether_shost[2] = p->ether_addr_octet[2];
  (*ethh)->ether_shost[3] = p->ether_addr_octet[3];
  (*ethh)->ether_shost[4] = p->ether_addr_octet[4];
  (*ethh)->ether_shost[5] = p->ether_addr_octet[5];
}

/*---------------------------------------------------------------------*/
/*
 * Prepares ARP packet in the buffer passed as parameter
 */
void prepare_arp_packet(struct arp_pkt *arp_pkt, const uint32_t *src_ip, const uint32_t *dest_ip, struct ether_addr *src_mac, struct ether_addr *dest_mac, uint16_t htype) {
    memcpy(arp_pkt->eh.ether_shost, src_mac,  6);
    memcpy(arp_pkt->eh.ether_dhost, dest_mac,  6);
    arp_pkt->eh.ether_type = htons(ETHERTYPE_ARP);

    arp_pkt->ah.htype = htons (1);
    arp_pkt->ah.ptype =  htons (ETHERTYPE_IP);
    arp_pkt->ah.hlen = 6;
    arp_pkt->ah.plen = 4;
    arp_pkt->ah.opcode = htype;

    arp_pkt->ah.sender_ip = *src_ip;
    arp_pkt->ah.target_ip = *dest_ip;

    memcpy(arp_pkt->ah.sender_mac, src_mac,  6);
    if (ntohs(htype) == 1) {
        memset (arp_pkt->ah.target_mac, 0, 6 * sizeof (uint8_t));
    } else {
        memcpy(arp_pkt->ah.target_mac, dest_mac,  6);
    }
}


void arp_reply(struct arp_pkt *arppkt) {
    unsigned  char *tx_buf = NETMAP_BUF(send_ring, send_ring->slot[send_ring->cur].buf_idx);
    struct netmap_slot *slot = &send_ring->slot[send_ring->cur];
    struct arp_pkt *arp_reply = (struct arp_pkt *)(tx_buf);
    struct ether_addr d;
    memcpy(&d, (struct ether_addr *)arppkt->ah.sender_mac, 6);
    struct ether_addr s = *ether_aton_src(src_mac);
    prepare_arp_packet(arp_reply, &arppkt->ah.target_ip, &arppkt->ah.sender_ip, &s, &d, htons(2));
    slot->len = sizeof(struct arp_pkt);
    send_ring->cur = nm_ring_next(send_ring, send_ring->cur);
    send_ring->head = send_ring->cur;
    ioctl(fds.fd, NIOCTXSYNC, NULL);
    char arp_src_ip[INET_ADDRSTRLEN];
    char arp_target_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(arp_reply->ah.target_ip), arp_target_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(arp_reply->ah.sender_ip), arp_src_ip, INET_ADDRSTRLEN);
    
}

void arp_request(const uint32_t *dest_ip) {
    unsigned  char *tx_buf = NETMAP_BUF(send_ring, send_ring->slot[send_ring->cur].buf_idx);
    struct netmap_slot *slot = &send_ring->slot[send_ring->cur];
    struct arp_pkt *arp_request_pkt = (struct arp_pkt *)(tx_buf);
    uint32_t source_ip;
    inet_pton(AF_INET, src_ip, &(source_ip));
    struct ether_addr source_mac = *ether_aton_src(src_mac);
    struct ether_addr dest_mac = *ether_aton_dst("ff:ff:ff:ff:ff:ff");
    prepare_arp_packet(arp_request_pkt, &source_ip, dest_ip, &source_mac, &dest_mac, htons(1));

    slot->len = sizeof(struct arp_pkt);
    send_ring->cur = nm_ring_next(send_ring, send_ring->cur);
    send_ring->head = send_ring->cur;
    ioctl(fds.fd, NIOCTXSYNC, NULL);
}
void handle_arp_packet(char* buffer){
	// make entry in arp cache
	    struct arp_pkt *arppkt;
      struct ether_addr sender_mac;
      arppkt = (struct arp_pkt *)buffer;
      char arp_target_ip[INET_ADDRSTRLEN];
      char arp_sender_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(arppkt->ah.target_ip), arp_target_ip, INET_ADDRSTRLEN);
      inet_ntop(AF_INET, &(arppkt->ah.sender_ip), arp_sender_ip, INET_ADDRSTRLEN);
      
      memcpy(&sender_mac, (struct ether_addr *)arppkt->ah.sender_mac, 6);
      insert_arp_cache(arppkt->ah.sender_ip, sender_mac);
      if(strcmp(arp_target_ip, src_ip) == 0){
      	if (ntohs(arppkt->ah.opcode) == ARP_REQUEST) {
            arp_reply(arppkt);
        }
        if (ntohs(arppkt->ah.opcode) == ARP_REPLY) {
            // printf("ARP REPLY packet from: %s\n", arp_sender_ip);
            // printf("ARP REPLY sender mac:%02x:%02x:%02x:%02x:%02x:%02x\n", arppkt->eh.ether_shost[0], arppkt->eh.ether_shost[1],
              //      arppkt->eh.ether_shost[2], arppkt->eh.ether_shost[3], arppkt->eh.ether_shost[4], arppkt->eh.ether_shost[5]);
        }
      }
}
/*
get memory from writing packet
getPktBuf
*/
char* writePktmem(int id){
	char *dst = NETMAP_BUF(send_ring, send_ring->slot[send_ring->cur].buf_idx);
	return dst;
}
void setSlotLen(int length) {
  send_ring->slot[send_ring->cur].len = length;
}
/*
connect as a client to another VNF
*/
int createClient(int id, string local_ip , string remoteServerIP, int remoteServerPort, string protocol){
	//netmap fd passed instaed of id
	map_index = map_index + 1; 
	conn_map[map_index] = remoteServerIP;
	
	return map_index;
}
/*
put packets into the netmap tx ring which would be pushed to NIC in the while loop in startEventLoop function
*/
void sendData(int connID, int id, char* packetToSend, int size){
	struct ether_header *ethh = (struct ether_header *)packetToSend;
	struct ether_addr backend_mac;
  struct arp_cache_entry *entry;
  int i;
  uint32_t dst_ip;
 	inet_pton(AF_INET, (conn_map[id]).c_str(), &(dst_ip));
	for (i = 0; i < ARP_CACHE_LEN; i++) {
      	entry = &arp_cache[i];
      	if (entry->ip == dst_ip ) {
          backend_mac = entry->mac;
          break;
      	}
      }
      if(i == ARP_CACHE_LEN) {
      	arp_request(&dst_ip);
        return;
      }
      change_dst_mac(&ethh, &backend_mac);
      change_src_mac(&ethh, src_byte);
      send_ring->cur = nm_ring_next(send_ring, send_ring->cur);
}
/*function for clearing the TX ring in netmap. called in startEventLoop function*/
void send_batch() {
  send_ring->head = send_ring->cur;
  ioctl(fds.fd, NIOCTXSYNC, NULL);
}
static void
sigint_h(int sig)
{
    (void)sig;  /* UNUSED */
    do_abort = 1;
    nm_close(lib_netmap_desc);
    signal(SIGINT, SIG_DFL);
}
/*initilize netmap with the interface provided as parameter*/
int createServer(string inter_face, string server_ip, int server_port, string protocol){
    src_ip1 = server_ip.c_str();
    memset(arp_cache, 0, ARP_CACHE_LEN * sizeof(struct arp_cache_entry));
    struct nmreq base_req;
    memset(&base_req, 0, sizeof(base_req));
    base_req.nr_flags |= NR_ACCEPT_VNET_HDR;
    string iface = "netmap:";
    string if_name = iface + inter_face;
    lib_netmap_desc= nm_open(if_name.c_str(), &base_req, 0, 0);
    fds.fd = NETMAP_FD(lib_netmap_desc);
    fds.events = POLLIN;
    receive_ring = NETMAP_RXRING(lib_netmap_desc->nifp, 0);
    send_ring = NETMAP_TXRING(lib_netmap_desc->nifp, 0);
    
     if(ds_sizing==1){
	        storageds.add_block(&mp_ds.front(), mp_ds.size(), 64);
		ds_sizing=0;
	}
	
    return fds.fd;
}
/*polls the NIC for packets received. 
copies packet pointer to RX ring of netmap
copies packet pointers from TX ring of netmap to NIC
*/
void startEventLoop(){
    fn fn_ptr;
    int r;
    char *src;
    int my_fd = fds.fd;
    signal(SIGINT, sigint_h);
    std::vector<char> mp_v1;
	std::vector<char> mp_v2;
	std::vector<char> mp_v3;
	std::vector<char> mp_v4;
	if(memory_size[0] != 0){
	 mp_v1.resize((memory_size[0])*2097152);
   cout<<"vector size is "<<mp_v1.size()<<endl;
   storage1.add_block(&mp_v1.front(), mp_v1.size(), memory_size[0]);  //uncomment nov22
	}
	if(memory_size[1]!=0){
    mp_v2.resize((memory_size[1])*2097152);
    cout<<"vector size is "<<mp_v2.size()<<endl;
    storage2.add_block(&mp_v2.front(), mp_v2.size(), memory_size[1]);  //uncomment nov22
  }
	if(memory_size[2]!=0){
    mp_v3.resize((memory_size[2])*2097152);
    cout<<"vector size is "<<mp_v3.size()<<endl;
    storage3.add_block(&mp_v3.front(), mp_v3.size(), memory_size[2]);  //uncomment nov22
  }
	if(memory_size[3]!=0){
    mp_v4.resize((memory_size[3])*2097152);
    cout<<"vector size is "<<mp_v4.size()<<endl;
    storage4.add_block(&mp_v4.front(), mp_v4.size(), memory_size[3]);  //uncomment nov22
  }
  src_byte = ether_aton_src(src_mac);
  int n, rx;
  int cur = receive_ring->cur;
  while (do_abort) {
        poll(&fds,  1, -1); 
        n = nm_ring_space(receive_ring);
        for(rx = 0; rx < n; rx++) {
          struct netmap_slot *slot = &receive_ring->slot[cur];
          src = NETMAP_BUF(receive_ring, slot->buf_idx);
          length = slot->len;
          fn_ptr = funct_ptr[my_fd];
          mem_ptr[my_fd] = NULL; //memory for request object
          fn_ptr(my_fd, length, NULL, src); //length pass instead of id
          cur = nm_ring_next(receive_ring, cur);
        }
        receive_ring->head = receive_ring->cur = cur;
        send_batch();
    }
}
/*allocate a memory block for request object
allocReqObj
*/
void* allocReqCtxt(int alloc_sockid, int id, int index){
        client_list[alloc_sockid] = alloc_sockid;
	if(index==1){
	      mem_ptr[alloc_sockid] = static_cast<void*>(storage1.malloc());    //lock TODO
	}
	else if(index==2){
	     	mem_ptr[alloc_sockid] = static_cast<void*>(storage2.malloc());    //lock TODO
	}
	else if(index==3){
        mem_ptr[alloc_sockid] = static_cast<void*>(storage3.malloc());    //lock TODO
  }
	else if(index==4){
        mem_ptr[alloc_sockid] = static_cast<void*>(storage4.malloc());    //lock TODO
  }
  if(mem_ptr[alloc_sockid]==0){
        cout<<"could not malloc"<<endl;
  }
	return mem_ptr[alloc_sockid];

}
/*
free up memory of request object
freeReqObj
*/
void freeReqCtxt(int alloc_sockid, int id, int index){
	 got = client_list.find(alloc_sockid);
   if (got == client_list.end()){
   mem_ptr.erase(alloc_sockid);
   }   
   else{
   if(index==1){
        storage1.free(static_cast<void*>(mem_ptr[alloc_sockid]));
	 }
	 else if(index==2){
	      storage2.free(static_cast<void*>(mem_ptr[alloc_sockid]));
	 }
	 else if(index==3){
        storage3.free(static_cast<void*>(mem_ptr[alloc_sockid]));
             }
	 else if(index==4){
        storage4.free(static_cast<void*>(mem_ptr[alloc_sockid]));
    }
    mem_ptr.erase(alloc_sockid);  //uncomment nov22
    client_list.erase(alloc_sockid);

   }

}
/*
set data in data store. currently only local storage.TODO develop a stack to communicate with data store
*/
void setData(int connID, int id, int key, string localRemote, string value){
	if(localRemote=="local"){
	
		value = value + '\0';
		void* setds;
		ds_lock.lock();
		if(ds_size==ds_threshold){
        free_ds_pool();
    }	
		setds = storageds.malloc();
		ds_size++;
    memcpy(setds,value.c_str(),value.length());
    cout<<"setdata"<<endl;
    ds_map1[key] = setds;
    local_list[setds] = key;
		ds_lock.unlock();
	}
}
/*
retreive data from data store. currently only local storage.TODO develop a stack to communicate with data store
*/
void getData(int connID, int id, int key, string localRemote, void callbackFnPtr(int, int,  void*, char*)){
	registerCallback(connID, id, "read", callbackFnPtr);
	if(localRemote=="local"){
		
		 fn fn_ptr;
		 char* ds_value;
		 ds_lock.lock();
		 ds_value = static_cast<char*>(ds_map1[key]);
		 ds_lock.unlock();
	   fn_ptr = funct_ptr[connID];
     fn_ptr(connID, id, mem_ptr[connID], ds_value);
                
		
	}
}

