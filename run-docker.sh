sudo dockerd
docker ps
docker build --tag 6.828 .
docker image ls
docker rmi $ID
docker run -d --name=6.828 -v /home/huiquan/xv6-labs-2018:/usr/src/app --rm -it 6.828:latest
docker exec  -it 6.828 bash