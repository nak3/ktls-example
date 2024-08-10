#define _GNU_SOURCE

#include <sys/sendfile.h>
#include <openssl/md5.h>

#include <ktls_server.h>

#define PORT 4433
#define CRT_PEM "cert.pem"
#define KEY_PEM "key.pem"


void init_openssl()
{
	SSL_library_init();
	SSL_load_error_strings();
	ERR_load_crypto_strings();
#if OPENSSL_VERSION_NUMBER >= 0x30000000L && !defined(LIBRESSL_VERSION_NUMBER)
#else
	ERR_load_BIO_strings();
#endif
	OpenSSL_add_ssl_algorithms();
}

void clean_openssl()
{
	EVP_cleanup();
}

SSL_CTX* init_server_ctx(void)
{
	const SSL_METHOD *method = NULL;
	SSL_CTX *ctx = NULL;

	method = TLS_server_method();

	ctx = SSL_CTX_new(method);
	if (!ctx) {
		perror("Unable to SSL_CTX_new");
		ERR_print_errors_fp(stderr);
	}
	return ctx;
}

int load_certificates(SSL_CTX* ctx, char *cert_file, char *key_file)
{
	int rc = -1;

	if (SSL_CTX_use_certificate_file(ctx, cert_file, SSL_FILETYPE_PEM) != 1) {
		ERR_print_errors_fp(stderr);
		goto end;
	}
	if (SSL_CTX_use_PrivateKey_file(ctx, key_file, SSL_FILETYPE_PEM) != 1) {
		ERR_print_errors_fp(stderr);
		goto end;
	}

	if (SSL_CTX_check_private_key(ctx) != 1) {
		ERR_print_errors_fp(stderr);
		goto end;
	}

	rc = 0;
end:
	return rc;
}

#if OPENSSL_VERSION_NUMBER <= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
int setup_ktls(int client, SSL *ssl)
{

	int rc = -1;
	struct tls12_crypto_info_aes_gcm_128 crypto_info;

	bzero(&crypto_info, sizeof(crypto_info));

	EVP_CIPHER_CTX * write_ctx = ssl->enc_write_ctx;
	EVP_AES_GCM_CTX* gcm_write = (EVP_AES_GCM_CTX*)(write_ctx->cipher_data);

	unsigned char* key_write = (unsigned char*)(gcm_write->gcm.key);
	unsigned char* iv_write = gcm_write->iv;
	unsigned char* seq_number_write = ssl->s3->write_sequence;

	crypto_info.info.version = TLS_1_2_VERSION;
	crypto_info.info.cipher_type = TLS_CIPHER_AES_GCM_128;

	memcpy(crypto_info.iv, iv_write + 4, TLS_CIPHER_AES_GCM_128_IV_SIZE);
	memcpy(crypto_info.rec_seq, seq_number_write, TLS_CIPHER_AES_GCM_128_REC_SEQ_SIZE);
	memcpy(crypto_info.key, key_write, TLS_CIPHER_AES_GCM_128_KEY_SIZE);
	memcpy(crypto_info.salt, iv_write, TLS_CIPHER_AES_GCM_128_SALT_SIZE);

	/* set ktls */
	if (setsockopt(client, SOL_TCP, TCP_ULP, "tls", sizeof("tls")) <0) {
		perror("Unable set TCP_ULP");
		goto end;
	}
	if (setsockopt(client, SOL_TLS, TLS_TX, &crypto_info, sizeof(crypto_info)) < 0) {
		perror("Unable set TLS_TX");
		goto end;
	}
	rc = 0;
end:
	return rc;
}
#endif

int create_ktls_server(int port)
{
	int rc = -1, reuse = 1;
	struct sockaddr_in addr;

	int fd = socket(PF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("Unable to create socket");
		goto end;
	}

	bzero(&addr, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);

	rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));
	if (rc < 0) {
		perror("Unable to set SO_REUSEADDR");
		goto end;
	}

	rc = bind(fd, (const struct sockaddr*)&addr, sizeof(addr));
	if (rc < 0) {
		perror("Unable to bind");
		goto end;
	}

	rc = listen(fd, 10);
	if (rc < 0) {
		perror("Unable to listen");
		goto end;
	}
	rc = 0;
end:
	if (rc < 0 && fd >= 0) {
		close(fd);
		fd = -1;
	}
	return fd;
}

int create_connection(char *host, int port)
{
	int rc = -1;
	struct sockaddr_in addr;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) goto end;

	bzero(&addr, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(host);

	printf("connecting to %s:%d\n", host, port);

	rc = connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_in));
	if (rc < 0) {
		perror("Connect: ");
		goto end;
	} else {
		printf("connected\n");
	}
end:
	if (rc < 0 && fd >= 0) {
		close(fd);
		fd = -1;
	}
	return fd;

}

int do_sslwrite(int client, char *file, SSL *ssl)
{
	int rc = -1;
	struct stat st;
	clock_t start = 0, end = 0;
	double cpu_time_used = 0.;

	printf("start do_sslwrite(%s)\n", file);

	int fd = open(file, O_RDONLY);
	if (fd < 0) goto end;

	if (fstat(fd, &st) < 0)  goto end;

	off_t len = st.st_size;
	size_t buf_size = 4096;
	unsigned char buf[4096] = {};

	start = clock();

	while (len > 0) {
		rc = read(fd, buf, buf_size);
		if (rc < 0) {
			perror("read file failed.");
			goto end;
		}
		rc = SSL_write(ssl, buf, rc);
		if (rc < 0) {
			perror("SSL_write file failed.");
			goto end;
		}

		len -= buf_size;
	}

	end = clock();

	cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
	printf("SSL_write cost time: %.02f\n", cpu_time_used);

	rc = 0;
end:
	if (fd > 0) close(fd);

	printf("end do_sslwrite(%s)\n", file);

	return rc;
}

int do_send(int client, char *file, SSL* ssl)
{
	int rc = -1;
	struct stat st;
	clock_t start = 0, end = 0;
	double cpu_time_used = 0.;

	printf("start do_send(%s)\n", file);

	int fd = open(file, O_RDONLY);
	if (fd < 0) goto end;

	if (fstat(fd, &st) < 0)  goto end;

	off_t len = st.st_size;
	size_t buf_size = 4096;
	unsigned char buf[4096] = {};

	start = clock();

	while (len > 0) {
		rc = read(fd, buf, buf_size);
		if (rc < 0) {
			perror("read file failed.");
			goto end;
		}
		rc = send(client, buf, rc, 0);
		if (rc < 0) {
			perror("send file failed.");
			goto end;
		}

		len -= buf_size;
	}

	end = clock();

	cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
	printf("send cost time: %.02f\n", cpu_time_used);

	rc = 0;
end:
	if (fd > 0) close(fd);

	printf("end do_send(%s)\n", file);

	return rc;

}

int do_sendfile(int client, char *file, SSL* ssl)
{
	int rc = -1;
	struct stat st;
	clock_t start = 0, end = 0;
	double cpu_time_used = 0.;

	printf("start do_sendfile(%s)\n", file);

	int fd = open(file, O_RDONLY);
	if (fd < 0) goto end;

	if (fstat(fd, &st) < 0)  goto end;

	off_t len = st.st_size;
	off_t offset = 0;
	size_t buf_size = 4096;

	start = clock();

	while (len > 0) {
		rc = sendfile(client, fd, &offset, buf_size);
		if (rc < 0) {
			perror("sendfile failed.");
			goto end;
		}
		len -= buf_size;
	}

	end = clock();

	cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
	printf("sendfile cost time: %.02f\n", cpu_time_used);

	rc = 0;
end:
	if (fd > 0) close(fd);

	printf("end do_sendfile(%s)\n", file);

	return rc;
}

int do_splice(int client, char *file, SSL* ssl)
{
	int rc = -1, fd = -1;
	int p[2] = {};
	clock_t start = 0, end = 0;
	double cpu_time_used = 0.;
	struct stat st;

	printf("start do_splice(%s)\n", file);

	if(pipe(p) < 0) {
		perror("Unable to create a pipe");
		goto end;
	}

	fd = open(file, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Unable to open(%s): %s", file, strerror(errno));
		goto end;
	}

	if (fstat(fd, &st) < 0)  goto end;

	off_t len = st.st_size;
	off_t offset = 0;
	size_t buf_size = 4096;

	start = clock();

	while(len > 0) {

		rc = splice(fd, &offset, p[1], NULL, buf_size, SPLICE_F_MOVE | SPLICE_F_MORE);
		if (rc < 0) {
			perror("splice read failed.");
			goto end;
		}

		rc = splice(p[0], NULL, client, NULL, buf_size, SPLICE_F_MOVE | SPLICE_F_MORE);

		if (rc < 0) {
			perror("splice write failed.");
			goto end;
		}
		len -= buf_size;
	}

	end = clock();

	cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
	printf("splice cost time: %.02f\n", cpu_time_used);

	rc = 0;
end:
	if (fd > 0) close(fd);
	if (p[0] > 0) close(p[0]);
	if (p[1] > 0) close(p[1]);

	printf("end do_splice(%s)\n", file);

	return rc;
}


int checksum(char *file1, char *file2)
{
	int i = 0, j = 0, rc = -1;
	unsigned char c1[MD5_DIGEST_LENGTH] = {};
	unsigned char c2[MD5_DIGEST_LENGTH] = {};

	FILE *f1 = NULL;
	FILE *f2 = NULL;

	f1 = fopen(file1, "rb");
	if (!f1) goto end;

	f2 = fopen(file2, "rb");
	if (!f2) goto end;

	EVP_MD_CTX *md5_ctx;

	md5_ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(md5_ctx, EVP_md5(), NULL);

	size_t bytes = 0;
	unsigned char buf[BUFSIZ] = {};
	while ((bytes = fread (buf, 1, sizeof(buf), f1)) != 0) {
		EVP_DigestUpdate(md5_ctx, buf, bytes);
	}

	EVP_DigestFinal_ex(md5_ctx, c1, NULL);
	EVP_MD_CTX_free(md5_ctx);

	bzero(&buf, sizeof(buf));

	md5_ctx = EVP_MD_CTX_new();
	EVP_DigestInit_ex(md5_ctx, EVP_md5(), NULL);

	while ((bytes = fread (buf, 1, sizeof(buf), f2)) != 0) {
		EVP_DigestUpdate(md5_ctx, buf, bytes);
	}
	EVP_DigestFinal_ex(md5_ctx, c2, NULL);
	EVP_MD_CTX_free(md5_ctx);

	printf("checksum(%s): ", file1);
	for(i = 0; i < MD5_DIGEST_LENGTH; i++) printf("%02x", c1[i]);
	printf("\n");

	printf("checksum(%s): ", file2);
	for(i = 0; i < MD5_DIGEST_LENGTH; i++) printf("%02x", c2[i]);
	printf("\n");

	for (i=0, j=0; i<MD5_DIGEST_LENGTH && j<MD5_DIGEST_LENGTH; i++, j++) {
		if (c1[i] != c2[j]) goto end;
	}

	rc = 0;
end:
	return  rc;
}

int do_recv(int server, SSL *ssl, char *orig_file)
{
	int rc = -1;
	char tmpfile[] = ".TMP_ktls";

	mode_t mode = S_IRWXU | S_IRUSR | S_IROTH;
	int fd = open(tmpfile, O_WRONLY | O_CREAT | O_TRUNC, mode);

	if (fd < 0) goto end;

	SSL_set_fd(ssl, server);

	if ( SSL_connect(ssl) != 1 ) {
		ERR_print_errors_fp(stderr);
		goto end;
	}


	char buf[BUFSIZ] = {};
	int recv = 0;


	do {
		recv = SSL_read(ssl, buf, sizeof(buf));
		if (recv < 0) {
			ERR_print_errors_fp(stderr);
			goto end;
		}

		rc  = write(fd, buf, recv);
		if (rc < 0) {
			fprintf(stderr, "write(%s) failed. %s\n", tmpfile, strerror(errno));
			goto end;
		}

	} while(recv > 0);

	if (checksum(tmpfile, orig_file) != 0) {
		fprintf(stderr, "checksum(%s, %s) failed.", tmpfile, orig_file);
		goto end;
	}

	rc = 0;
end:
	if (fd >= 0) close(fd);

	if (unlink(tmpfile) < 0) {
		fprintf(stderr, "unlink(%s) failed. %s\n", tmpfile, strerror(errno));
	}

	return 0;
}

void main_server(int port, char *file, int count, int enable_ktls, do_tls tls_send)
{
	int rc = -1, server = -1, i = 0;
	SSL_CTX *ctx = NULL;
	SSL *ssl = NULL;

	init_openssl();

	ctx = init_server_ctx();
	if (!ctx) goto end;

	rc = SSL_CTX_use_certificate_chain_file(ctx, CRT_PEM);
	if (rc != 1) goto end;

	rc = SSL_CTX_use_PrivateKey_file(ctx, KEY_PEM, SSL_FILETYPE_PEM);
	if (rc != 1) goto end;

	//rc = SSL_CTX_set_cipher_list(ctx, "ECDH-ECDSA-AES128-GCM-SHA256");
	// For TLS 1.2
//	rc = SSL_CTX_set_cipher_list(ctx, "AES128-GCM-SHA256");
	// For TLS 1.3
	rc = SSL_CTX_set_ciphersuites(ctx, "TLS_AES_128_GCM_SHA256");
	if (rc != 1) goto end;

	SSL_CTX_set_options(ctx, SSL_OP_ENABLE_KTLS);

	server = create_ktls_server(port);
	if (server < 0) goto end;

	for (i=0; i<count; i++) {
		struct sockaddr_in addr;
		unsigned int len = sizeof(addr);
		int client = accept(server, (struct sockaddr*)&addr, &len);
		if (client < 0) goto end;

		ssl = SSL_new(ctx);
		if (!ssl) goto loop_done;

		SSL_set_fd(ssl, client);

		if (SSL_accept(ssl) != 1) {
			ERR_print_errors_fp(stderr);
			goto end;
		}

#if OPENSSL_VERSION_NUMBER <= 0x10100000L && !defined(LIBRESSL_VERSION_NUMBER)
		if (enable_ktls && setup_ktls(client, ssl) < 0) {
			ERR_print_errors_fp(stderr);
			goto end;
		}
#endif

		rc = tls_send(client, file, ssl);
		if (rc < 0) goto loop_done;

	loop_done:
		SSL_free(ssl);
		if (client >= 0) close(client);
	}
end:
	if (server >= 0) close(server);

	SSL_CTX_free(ctx);
	clean_openssl();
	return;
}


void main_client(char *host, int port, char *orig_file)
{
	SSL_CTX *ctx = NULL;
	SSL *ssl = NULL;
	int rc = -1, server = -1;

	init_openssl();

	ctx = SSL_CTX_new(TLS_client_method());
	if (!ctx) goto end;

//	SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

	//rc = SSL_CTX_set_cipher_list(ctx, "ECDH-ECDSA-AES128-GCM-SHA256");
	// For TLS 1.2
//	rc = SSL_CTX_set_cipher_list(ctx, "AES128-GCM-SHA256");
	// For TLS 1.3
	rc = SSL_CTX_set_ciphersuites(ctx, "TLS_AES_128_GCM_SHA256");
	if (rc != 1) goto end;

	SSL_CTX_set_options(ctx, SSL_OP_ENABLE_KTLS);

	ssl = SSL_new(ctx);
	if (!ssl) goto end;

	server = create_connection(host, port);
	if (server < 0) goto end;

	rc = do_recv(server, ssl, orig_file);
	if (rc < 0) goto end;

end:
	if (server >= 0) close(server);

	SSL_CTX_free(ctx);
	clean_openssl();
	return;
}

int main_func(int argc, char *argv[], int enable_ktls, do_tls tls_send)
{
	char *host = NULL;
	char *file = NULL;
	pid_t pid;

	int status, count = 3, rc = -1, i = 0;

	if (argc != 3) {
		perror("usage: cmd host file");
		exit(EXIT_FAILURE);
	}

	host = argv[1];
	file = argv[2];

	pid = fork();

	if (pid == 0) {
		printf("main_client\n");
		sleep(1);
		for (i=0; i<count; i++) {
			main_client(host, PORT, file);
			sleep(1);
		}
	} else {
		printf("main_server\n");
		main_server(PORT, file, count, enable_ktls, tls_send);
		if ((pid = wait(&status)) == -1) {
			perror("wait error");
			goto end;
		}
	}
	rc = 0;
end:
	return rc;

}

