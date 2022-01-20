FROM ubuntu:20.04

ENV TZ=Asia/Yekaterinburg
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get update  
RUN apt-get -y install \
				git \
				locales \
				apt-utils \
				binutils-arm-none-eabi \
				gcc-arm-none-eabi \
				make \
				stlink-tools \
				default-jre


ENV LANGUAGE C.UTF-8
ENV LANG C.UTF-8
ENV LC_ALL C.UTF-8
RUN dpkg-reconfigure locales

# Install fmpp utility (http://fmpp.sourceforge.net/) for ChibiOS code generators.
ADD fmpp_0.9.16.tar.gz /opt/
RUN ln -s /opt/fmpp/bin/fmpp /usr/bin/fmpp

#create user with same uid as host user
RUN addgroup --gid 1000 username
RUN adduser --shell /bin/bash --uid 1000 --gid 1000 --disabled-password --gecos '' username
RUN adduser username sudo 
RUN echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers
RUN echo "username:1234" | chpasswd

CMD su username -c /bin/bash
