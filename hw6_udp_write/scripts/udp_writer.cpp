#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <locale>
#include <stdlib.h>
#include <fcntl.h>

#if defined(linux)
#   include <sys/io.h>
#   include <sys/time.h>
#   define SET_BINARY_MODE(file)
#else
#   include <io.h>
#   include <time.h>
#   define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#endif

#define PCAP_HEADER_BYTES       24
#define PCAP_DATA_HEADER_BYTES  16
#define ETH_DST_ADDR_BYTES      6
#define ETH_SRC_ADDR_BYTES      6
#define ETH_PROTOCOL_BYTES      2
#define IP_VERSION_BYTES        1
#define IP_HEADER_BYTES         1
#define IP_TYPE_BYTES           1
#define IP_LENGTH_BYTES         2
#define IP_ID_BYTES             2
#define IP_FLAG_BYTES           2
#define IP_TIME_BYTES           1
#define IP_PROTOCOL_BYTES       1
#define IP_CHECKSUM_BYTES       2
#define IP_SRC_ADDR_BYTES       4
#define IP_DST_ADDR_BYTES       4
#define UDP_DST_PORT_BYTES      2
#define UDP_SRC_PORT_BYTES      2
#define UDP_LENGTH_BYTES        2
#define UDP_CHECKSUM_BYTES      2
#define IP_PROTOCOL_DEF        0x0800
#define IP_VERSION_DEF         0x4
#define IP_HEADER_LENGTH_DEF   0x5
#define IP_TYPE_DEF            0x0
#define IP_FLAGS_DEF           0x4
#define TIME_TO_LIVE           0xe
#define UDP_PROTOCOL_DEF       0x11

using std::string;


typedef struct pcap_hdr_s {
    int magic_number;   /* magic number */
    short version_major;  /* major version number */
    short version_minor;  /* minor version number */
    int thiszone;       /* GMT to local correction */
    int sigfigs;        /* accuracy of timestamps */
    int snaplen;        /* max length of captured packets, in octets */
    int network;        /* data link type */
} pcap_hdr_t;

typedef struct pcaprec_hdr_s {
    int ts_sec;         /* timestamp seconds */
    int ts_usec;        /* timestamp microseconds */
    int incl_len;       /* number of octets of packet saved in file */
    int orig_len;       /* actual length of packet */
} pcaprec_hdr_t;

unsigned short udp_sum_calc( unsigned char *ip_src_addr, unsigned char *ip_dst_addr, unsigned char *ip_protocol, 
                             unsigned char *ip_length, unsigned char *udp_src_port, unsigned char *udp_dst_port, 
                             unsigned char *udp_length, unsigned char *udp_data)
{
    unsigned short padd = 0;
    unsigned int sum = 0;	
    int udp_len = ((udp_length[0] << 8) | udp_length[1]);
    int data_length = udp_len - (UDP_CHECKSUM_BYTES + UDP_LENGTH_BYTES + UDP_DST_PORT_BYTES + UDP_SRC_PORT_BYTES);

	// Find out if the length of data is even or odd number. If odd,
	// add a padding byte = 0 at the end of packet
	if ( (data_length & 1) == 1 )
    {
		padd=1;
		udp_data[data_length]=0;
	}
	
	// add the UDP pseudo header which contains the IP source and destinationn addresses
	for (int i=0; i<4; i=i+2)
    {
		sum += ((ip_src_addr[i]<<8)&0xFF00)+(ip_src_addr[i+1]&0xFF);
	}

    for (int i=0; i<4; i=i+2)
    {
		sum += ((ip_dst_addr[i]<<8)&0xFF00)+(ip_dst_addr[i+1]&0xFF); 	
	}

    sum += ip_protocol[0];
    sum += (((unsigned short)ip_length[0]<<8)&0xFF00)+(ip_length[1]&0xFF) - 20;
    sum += (((unsigned short)udp_dst_port[0]<<8)&0xFF00)+(udp_dst_port[1]&0xFF);
    sum += (((unsigned short)udp_src_port[0]<<8)&0xFF00)+(udp_src_port[1]&0xFF);
    sum += (((unsigned short)udp_length[0]<<8)&0xFF00)+(udp_length[1]&0xFF);

    // make 16 bit words out of every two adjacent 8 bit words and 
	// calculate the sum of all 16 bit words
	for (int i=0; i < data_length; i += 2)
    {
        sum += (((unsigned short)udp_data[i]<<8)&0xFF00);
        //printf( "%02x --> %08x\n", udp_data[i], sum );
        sum += (udp_data[i+1]&0xFF);
        //printf( "%02x --> %08x\n", udp_data[i+1], sum );
	}	

	// keep only the last 16 bits of the 32 bit calculated sum and add the carries
	while (sum >> 16 != 0)
    {
		sum = (sum & 0xFFFF) + (sum >> 16);
    }

	// Take the one's complement of sum
	sum = ~sum;
    //printf("sum: %08x\n", sum );

    return sum;
}

unsigned short ip_sum_calc(unsigned char *buff, int len_ip_header)
{
    unsigned short word16;
    unsigned int sum=0;
    unsigned short i;
    
	// make 16 bit words out of every two adjacent 8 bit words in the packet
	// and add them up
	for ( i=0; i<len_ip_header ; i=i+2 )
    {
		word16 =((buff[i]<<8)&0xFF00)+(buff[i+1]&0xFF);
		sum = sum + (unsigned int)word16;	
	}
	
	// take only 16 bits out of the 32 bit sum and add up the carries
	while ( sum >> 16 )
    {
	    sum = (sum & 0xFFFF)+(sum >> 16);
    }

	// one's complement the result
	sum = ~sum;
	
    return (unsigned short)sum;
}

void hexstr2bytes( const char *str, unsigned char *buf, int nbytes )
{
    unsigned char c = 0;
    unsigned char v = 0;
    int n = 0;

    *buf = 0;
    
    if ( str == NULL || *str == '\0' || strlen((char*)str) == 0 )
    {
        return;
    }
    
    while ( *str != '\0' && n < nbytes*2 )
    {
        if ( isalnum(*str) ) 
        {
            c = ((unsigned char)*str & 0xff) - 0x30;
            if ( c > 15 ) c -= 7;
            v = (v << 4) + (unsigned int)(c & 0x0f);

            if ( n % 2 == 1 ) 
            {   
                *buf = (*buf << 8) + v;
                v = 0;
                buf++;
            }
            n++;
        }
        str++;
    }
}

void decode_ip_addr( const char *str, unsigned char *ip_addr )
{
    unsigned char c = 0;
    unsigned int v = 0;
    int n = 0;

    if ( str == NULL || *str == '\0' )
    {
        return;
    }

    while ( *str != '\0' )
    {
        if ( *str == '.' ) 
        {   
            ip_addr[n++] = v;
            v = 0;
            str++;
        }

        c = *str - '0';
        v = (v * 10) + c;
        str++;
    } 

    ip_addr[n] = v;
}


void write_udp_packet( unsigned char *buffer, int size, const string &src_mac_addr, const string &src_ip_addr, const string &src_ip_port,
                       const string &dst_mac_addr, const string &dst_ip_addr, const string &dst_ip_port, unsigned char *packet )
{
    static int packet_num = 8189;

    int ip_start_addr = ETH_DST_ADDR_BYTES + ETH_SRC_ADDR_BYTES + ETH_PROTOCOL_BYTES; 
    int ip_length_addr = ip_start_addr + IP_VERSION_BYTES + IP_TYPE_BYTES;
    int ip_protocol_addr = ip_length_addr + IP_LENGTH_BYTES + IP_ID_BYTES + IP_FLAG_BYTES + IP_TIME_BYTES;
    int ip_src_addr = ip_protocol_addr + IP_PROTOCOL_BYTES + IP_CHECKSUM_BYTES;
    int ip_dst_addr = ip_src_addr + IP_SRC_ADDR_BYTES;
    int udp_src_port_addr = ip_dst_addr + IP_DST_ADDR_BYTES;
    int udp_dst_port_addr = udp_src_port_addr + UDP_SRC_PORT_BYTES;
    int udp_length_addr = udp_dst_port_addr + UDP_DST_PORT_BYTES;

    unsigned char * out = packet;

    // dst mac address
    hexstr2bytes( dst_mac_addr.c_str(), out, ETH_DST_ADDR_BYTES );
    out += ETH_DST_ADDR_BYTES;

    // src mac address
    hexstr2bytes( src_mac_addr.c_str(), out, ETH_SRC_ADDR_BYTES );
    out += ETH_SRC_ADDR_BYTES;

    // IP protocol
    *out++ = ((IP_PROTOCOL_DEF >> 8) & 0xff);
    *out++ = (IP_PROTOCOL_DEF & 0xff);
    
    // IP version & header length
    *out++ = ((IP_VERSION_DEF & 0xf) << 4) + (IP_HEADER_LENGTH_DEF & 0xf);

    // IP type
    *out++ = IP_TYPE_DEF;

    // IP length 
    int ip_length = 28 + size;
    *out++ = ((ip_length >> 8) & 0xff);
    *out++ = (ip_length & 0xff);

    // IP packet ID
    *out++ = ((packet_num >> 8) & 0xff);
    *out++ = (packet_num & 0xff);
    packet_num++;

    // flags - don't fragment
    *out++ = ((IP_FLAGS_DEF & 0xf) << 4);
    *out++ = 0;

    // time to live
    *out++ = TIME_TO_LIVE;

    // IP protocol type - UDP
    *out++ = UDP_PROTOCOL_DEF;

    *out = 0;
    *(out+1) = 0;

    // IP SRC address
    decode_ip_addr( src_ip_addr.c_str(), out+2 );

    // IP DST address
    decode_ip_addr( dst_ip_addr.c_str(), out+2+IP_SRC_ADDR_BYTES );

    // IP header checksum
    unsigned short ip_protocol_length = IP_VERSION_BYTES + IP_TYPE_BYTES + IP_LENGTH_BYTES + 
                                        IP_ID_BYTES + IP_FLAG_BYTES + IP_TIME_BYTES + IP_PROTOCOL_BYTES +
                                        IP_CHECKSUM_BYTES + IP_SRC_ADDR_BYTES + IP_DST_ADDR_BYTES;
    unsigned short ip_checksum = ip_sum_calc( &packet[ip_start_addr], ip_protocol_length );
    *out++ = ((ip_checksum >> 8) & 0xff);
    *out++ = (ip_checksum & 0xff);

    out += IP_SRC_ADDR_BYTES;
    out += IP_DST_ADDR_BYTES;

    // UDP dst port
    int dst_port = atoi( dst_ip_port.c_str() );
    *out++ = ((dst_port >> 8) & 0xff);
    *out++ = (dst_port & 0xff);

    // UDP src port
    int src_port = atoi( src_ip_port.c_str() );
    *out++ = ((src_port >> 8) & 0xff);
    *out++ = (src_port & 0xff);

    // UDP length
    int udp_length = size + (UDP_CHECKSUM_BYTES + UDP_LENGTH_BYTES + UDP_DST_PORT_BYTES + UDP_SRC_PORT_BYTES);
    *out++ = ((udp_length >> 8) & 0xff);
    *out++ = (udp_length & 0xff);

    // UDP checksum
    unsigned short udp_checksum = udp_sum_calc( &packet[ip_src_addr], &packet[ip_dst_addr], &packet[ip_protocol_addr], &packet[ip_length_addr], 
                                                &packet[udp_src_port_addr], &packet[udp_dst_port_addr], &packet[udp_length_addr], buffer );
    *out++ = ((udp_checksum >> 8) & 0xff);
    *out++ = (udp_checksum & 0xff);

    // UDP data
    for ( int i = 0; i < size; i++ )
    {
        out[i] = buffer[i];
    }
}

int main(int argc, char **argv)
{
    FILE * src_file;
    FILE * dst_file;

    // if reading pcap files
    unsigned char pcap_header[PCAP_HEADER_BYTES];
    unsigned char pcap_data_header[PCAP_DATA_HEADER_BYTES];

    SET_BINARY_MODE(stdin);
    src_file = fdopen(fileno(stdin), "rb");
    if (src_file == NULL) 
    {
        fputs("can't open stdin", stderr);
        return 1;
    }

    SET_BINARY_MODE(stdout);
    dst_file = fdopen(fileno(stdout), "wb");
    if (dst_file == NULL)
    {
        fputs("can't open stdout", stderr);
        return 1;
    }

    string src_mac = "00:15:c5:09:c7:fd";
    string src_ip_addr = "1.2.3.9";
    string src_port = "10012";

    string dst_mac = "00:0a:35:01:bf:4e";
    string dst_ip_addr = "1.2.3.4";
    string dst_port = "10012";

    pcap_hdr_t pcap_head = 
    {
        0xa1b2c3d4,
        2,
        4,
        0,
        0,
        65536,
        1
    };

    fwrite( &pcap_head, sizeof(pcap_head), 1, dst_file );
       
    unsigned char buffer[1024];
    unsigned char udp_packet[2048];
    int packet_length =  ETH_DST_ADDR_BYTES + ETH_SRC_ADDR_BYTES + ETH_PROTOCOL_BYTES + 
                         IP_VERSION_BYTES + IP_TYPE_BYTES + IP_LENGTH_BYTES + IP_ID_BYTES + 
                         IP_FLAG_BYTES + IP_TIME_BYTES + IP_PROTOCOL_BYTES + IP_CHECKSUM_BYTES + 
                         IP_SRC_ADDR_BYTES + IP_DST_ADDR_BYTES + UDP_DST_PORT_BYTES + 
                         UDP_SRC_PORT_BYTES + UDP_LENGTH_BYTES + UDP_CHECKSUM_BYTES;   
    
    
    struct timeval tv;
    gettimeofday(&tv, NULL);            
    int usec = 0;
    
    while ( !feof(src_file) )
    {
        int size = fread( buffer, 1, 1024, src_file );
    
        write_udp_packet( buffer, size, src_mac, src_ip_addr, src_port,
                          dst_mac, dst_ip_addr, dst_port, udp_packet );
              
        for ( int i = 0; i < size; i++ )
        {
            if ( i % 16 == 0 ) printf( "\n" );
            if ( i % 16 == 8 ) printf( " " );
            //printf( "%02x ", udp_packet[i] );
        }

        pcaprec_hdr_t pcap_phead = 
        {
            tv.tv_sec,
            usec++,
            packet_length+size,
            packet_length+size
        };
     
        fwrite( &pcap_phead, sizeof(pcap_phead), 1, dst_file );
        fwrite( udp_packet, 1, packet_length+size, dst_file );
    }
    
    fclose(src_file);
    fclose(dst_file);

    return 0;
}

