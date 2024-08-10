# KTLS example

## Prerequisite

* linux 4.13 or above
* openssl 3.x

## Generate self-signed certificate

```bash
$ openssl ecparam -out key.pem -genkey -name prime256v1
$ openssl req -x509 -new -key key.pem -out cert.pem
```

## Run the binaries

```bash
$ make
$ dd if=/dev/zero of=.dd.test bs=1M count=512
$ ./ktls_sendfile 127.0.0.1 .dd.test 
start do_sendfile(.dd.test)
sendfile cost time: 2.27
end do_sendfile(.dd.test)
checksum(.TMP_ktls): aa559b4e3523a6c931f08f4df52d58f2
checksum(.dd.test): aa559b4e3523a6c931f08f4df52d58f2 
```

## Reference

* This repository is forked from [crazyguitar/ktls-example](https://github.com/crazyguitar/ktls-example).
