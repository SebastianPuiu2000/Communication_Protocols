#include <queue.h>
#include "skel.h"

#define MAX_BITS 32

int interfaces[ROUTER_NUM_INTERFACES];

struct route_table_entry *route_table;
int route_table_length;

struct arp_entry *arp_table;
int arp_table_length;

struct queue *q;

struct mask_map *array;

int binary_search(struct route_table_entry *arr, int l, int r, __u32 dest_ip) {

	if(l > r)
		return -1;

	int mid = l + (r - l) / 2;

	if((arr[mid].mask & arr[mid].prefix) == (arr[mid].mask & dest_ip))
		return mid;
	else {
		if((arr[mid].mask & arr[mid].prefix) < (arr[mid].mask & dest_ip))
			return binary_search(arr, mid+1, r, dest_ip);
		else
			return binary_search(arr, l, mid-1, dest_ip);
	}
}

// functie ce numara cati biti de 1 se regasesc intr-un numar
int bit_1_counter(unsigned int n) {

	int count = 0;

	while(n != 0) {
		if(n & 01) 
			count++;

		n = n >> 1;
	}

	return count;
}

void initialize_array() {

	array = calloc(MAX_BITS+1, sizeof(struct mask_map));

	for(int i = 0; i <= MAX_BITS; i++) {
		array[i].vectors = calloc(1, sizeof(struct route_table_entry));
		array[i].size = 0;
		array[i].capacity = 1;
	}
}

void reallocate_array(int index, int old_size) {

	struct route_table_entry *newVectors = calloc(2 * old_size, sizeof(struct route_table_entry));

	for(int i = 0; i < old_size; i++)
		newVectors[i] = array[index].vectors[i];

	free(array[index].vectors);

	array[index].vectors = newVectors;
}

void insert_array(int index, struct route_table_entry entry) {

	// se verifica sa fie suficient spatiu pentru o noua inserare
	// in caz contrar, este dublata capacitatea
	if(array[index].size >= array[index].capacity) {
		reallocate_array(index, array[index].size);
		array[index].capacity = 2 * array[index].size;
	}

	array[index].vectors[array[index].size] = entry;
	array[index].size++;
}

// functie ce cauta un match pentru dest_ip
struct route_table_entry *get_route(int index, __u32 dest_ip) {

	// cazul in care nu exista elemente in care sa se caute
	if(array[index].size == 0)
		return NULL;

	int pos = binary_search(array[index].vectors, 0, array[index].size-1, dest_ip);

	// se verifica daca a fost gasite vreo ruta
	if(pos >= 0)
		return &array[index].vectors[pos];

	return NULL;
}

// functie ce aplica 'longest prefix match' pentru a gasi cea mai buna ruta
struct route_table_entry *get_best_route(__u32 dest_ip) {

	struct route_table_entry *best_route = NULL;

	// se parcurge descrescator pentru a respecta
	// criteriul de 'longest prefix match'
	for(int i = MAX_BITS; i >= 0; i--) {

		best_route = get_route(i, dest_ip);

		if(best_route != NULL)
			return best_route;
	}

	return NULL;
}

// functie necesara pentru extragerea unui ARP entry
struct arp_entry *get_arp_entry(__u32 ip) {

	for(int i = 0; i < arp_table_length; i++)
		if(arp_table[i].ip == ip)
			return &arp_table[i];

	return NULL;
}

// functie comparator necesara pentru functia qsort
int compare(const void *x1, const void *x2) {

    struct route_table_entry *y1 = (struct route_table_entry *)x1;
    struct route_table_entry *y2 = (struct route_table_entry *)x2;

    return (int)(y1->prefix - y2->prefix);
}

// functie pentru parsarea tabelei de rutare
int read_route_table(char file_name[50], struct route_table_entry *r_table) {

	FILE *f_in;
	f_in = fopen(file_name, "r");

	char line[200];
	int i = 0;

	for(i = 0; fgets(line, sizeof(line), f_in); i++) {

		char prefix_s[50], next_hop_s[50], mask_s[50];
		char interface_s[10];

		sscanf(line, "%s %s %s %s", prefix_s, next_hop_s, mask_s, interface_s);

		r_table[i].prefix = inet_addr(prefix_s);
		r_table[i].next_hop = inet_addr(next_hop_s); 
		r_table[i].mask = inet_addr(mask_s);
		r_table[i].interface = atoi(interface_s);
	}

	// sortare tabela de rutare dupa prefix
	qsort(r_table, i, sizeof(struct route_table_entry), compare);

	// impartire tabela de rutare pe categorii, in functie de masca
	for(int j = 0; j < i; j++) 
		insert_array(bit_1_counter(r_table[j].mask), r_table[j]);
	
	fclose(f_in);

	return i;
}

void no_arp_entry(struct route_table_entry *best_route, packet *pack) {

	struct ether_header *eth_hdr = calloc(1, sizeof(struct ether_header));

	// pregatire header de ethernet pentru transmiterea broadcast
	memset(eth_hdr->ether_dhost, 0xFF, ETH_ALEN);
	get_interface_mac(best_route->interface, eth_hdr->ether_shost);
	eth_hdr->ether_type = ntohs(ETHERTYPE_ARP);

	// trimitere ARP request
	send_arp(best_route->next_hop, inet_addr(get_interface_ip(best_route->interface)), 
			 eth_hdr, best_route->interface, ntohs(ARPOP_REQUEST));

	// realizare copie pachet si adaugare in coada
	packet *p = calloc(1, sizeof(packet));
	p->len = pack->len;
	memcpy(p->payload, pack->payload, pack->len);
	p->interface = pack->interface;

	queue_enq(q, p);
}

void no_route_error(struct iphdr *ip_hdr, struct ether_header *eth_hdr, packet *pack) {

	send_icmp_error(ip_hdr->saddr, inet_addr(get_interface_ip(pack->interface)), 
					eth_hdr->ether_dhost, eth_hdr->ether_shost, ICMP_DEST_UNREACH, 
					0, pack->interface);
}

void update_ip_hdr(struct iphdr *ip_hdr) {

	ip_hdr->ttl--;
	ip_hdr->check = 0;
	ip_hdr->check = ip_checksum(ip_hdr, sizeof(struct iphdr));
}

void update_arp_table(struct arp_header *arp_hdr) {
	
	int add_entry = 1;

	// verificare daca exista sau nu aceasta adresa in tabela arp
	for(int j = 0; j < arp_table_length; j++) {
		// daca s-a gasit adresa in tabela arp
		if(arp_table[j].ip == arp_hdr->spa) {
			// actualizare adresa:
			add_entry = 0;
			memcpy(arp_table[j].mac, arp_hdr->sha, 6);
			break;
		}
	}

	// daca nu s-a gasit adresa in tabela arp
	if(add_entry == 1) { 
		// adaugare adresa:
		arp_table[arp_table_length].ip = arp_hdr->spa;
		memcpy(arp_table[arp_table_length].mac, arp_hdr->sha, 6);
		arp_table_length++;
	}
}

int main(int argc, char *argv[])
{
	packet m;
	int rc;

	init(argc - 2, argv + 2);

	route_table = calloc(100000, sizeof(struct route_table_entry));
	arp_table = calloc(100000, sizeof(struct  arp_entry));

	initialize_array();

	route_table_length = read_route_table(argv[1], route_table);
	arp_table_length = 0;

	q = queue_create();

	while (1) {
		rc = get_packet(&m);
		DIE(rc < 0, "get_message");
		
		struct ether_header *eth_hdr = (struct ether_header *)m.payload;

		// se asigura faptul ca este un pachet ARP
		if(parse_arp(m.payload) != NULL) {

			struct arp_header *arp_hdr = parse_arp(m.payload);

			// se asigura faptul ca este un ARP request
			if(ntohs(arp_hdr->op) == ARPOP_REQUEST) {

				int i = 0, interface = 0;
				int router_intended = 0;

				while(i < ROUTER_NUM_INTERFACES) {
					if(arp_hdr->tpa == inet_addr(get_interface_ip(i))) {
						router_intended = 1;
						interface = i;
						break;
					}
					i++;
				}

				// se asigura faptul ca acest arp_request a fost destinat ruterului
				if(router_intended == 1) {

					struct ether_header *eth_hdr2 = malloc(sizeof(struct ether_header));

					// initializare ethernet header necesar
					get_interface_mac(interface, eth_hdr2->ether_shost);
					memcpy(eth_hdr2->ether_dhost, arp_hdr->sha, ETH_ALEN);
					eth_hdr2->ether_type = htons(ETHERTYPE_ARP);
					
					// trimitere arp_reply cu adresa mac a ruterului
					send_arp(arp_hdr->spa, arp_hdr->tpa, eth_hdr2, m.interface, htons(ARPOP_REPLY));
				}
			}

			// se asigura faptul ca este un ARP reply
			if(ntohs(arp_hdr->op) == ARPOP_REPLY) {

				// se realizeaza un uptate pentru tabela arp
				update_arp_table(arp_hdr);

				int q_size = queue_size(q);
				// se incearca trimiterea tuturor pachetelor care au ramas in coada
				while(q_size > 0) {

					packet *pack = queue_deq(q);

					struct ether_header *eth_hdr_q = (struct ether_header *) pack->payload;
					struct iphdr *ip_hdr_q = (struct iphdr *)(pack->payload + sizeof(struct ether_header));
	
					// cautare cea mai buna ruta
					struct route_table_entry *best_route = get_best_route(ip_hdr_q->daddr);
					if (best_route == NULL) {

						// nu a fost gasita ruta
						no_route_error(ip_hdr_q, eth_hdr_q, pack);
						continue;
					}

					// cautare arp_entry pentru next_hop
					struct arp_entry *arp_entry = get_arp_entry(best_route->next_hop);
					if (arp_entry == NULL) {
		
						// nu a fost gasit un arp_entry pentru next_hop
						no_arp_entry(best_route, pack);
						continue;
					} 

					// actualizare ethernet header
					memcpy(eth_hdr_q->ether_dhost, arp_entry->mac, ETH_ALEN);
					get_interface_mac(best_route->interface, eth_hdr_q->ether_shost);

					// trimitere pachet
					send_packet(best_route->interface, pack);	

					q_size--;
				}		
			}
		}
		else {

			struct iphdr *ip_hdr = (struct iphdr *)(m.payload + sizeof(struct ether_header));

			int i = 0, router_intended = 0;
			while(i < ROUTER_NUM_INTERFACES) {

				if(ip_hdr->daddr == inet_addr(get_interface_ip(i))) {
					router_intended = 1;
					break;
				}
				i++;
			}

			// se asigura faptul ca acest pachet IP a fost destinat ruterului
			if(router_intended == 1) {
				// se asigura faptul ca este un pachet ICMP
				if(parse_icmp(m.payload) != NULL) {

					struct icmphdr *icmp_hdr = parse_icmp(m.payload); 

					// se trimite echo_reply doar daca icmp header este de tipul echo
					if(icmp_hdr->type == ICMP_ECHO)
						send_icmp(ip_hdr->saddr, ip_hdr->daddr, eth_hdr->ether_dhost, eth_hdr->ether_shost, 
					          	ICMP_ECHOREPLY, 0, m.interface, icmp_hdr->un.echo.id, icmp_hdr->un.echo.sequence);
				}	
			}
			else {
				
				struct iphdr *ip_hdr = (struct iphdr *)(m.payload + sizeof(struct ether_header));

				// pachetul este aruncat daca ttl <= 1
				if(ip_hdr->ttl <= 1){

					get_interface_mac(m.interface, eth_hdr->ether_dhost);
					send_icmp_error(ip_hdr->saddr, inet_addr(get_interface_ip(m.interface)), eth_hdr->ether_dhost, eth_hdr->ether_shost, 
									ICMP_TIME_EXCEEDED, 0, m.interface);

					continue;
				}

				// pachetul este aruncat daca checksum e gresit
				if(ip_checksum(ip_hdr, sizeof(struct iphdr)) != 0){
					continue;
				}

				// actualizare ttl si checksum pentru ip header
				update_ip_hdr(ip_hdr);
					
				// cautare cea mai buna ruta
				struct route_table_entry *best_route = get_best_route(ip_hdr->daddr);
				if (best_route == NULL) {

					// nu a fost gasita ruta
					no_route_error(ip_hdr, eth_hdr, &m);
					continue;
				}

				// cautare arp_entry pentru next_hop
				struct arp_entry *arp_entry = get_arp_entry(best_route->next_hop);
				if (arp_entry == NULL) {

					// nu a fost gasit ARP entry
					no_arp_entry(best_route, &m);
					continue;
				} 

				// actualizare ethernet header
				memcpy(eth_hdr->ether_dhost, arp_entry->mac, ETH_ALEN);
				get_interface_mac(best_route->interface, eth_hdr->ether_shost);
	
				// trimitere pachet 
				send_packet(best_route->interface, &m);				
			}
		}
	}
}
