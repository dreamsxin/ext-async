# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|

  config.vm.provider "virtualbox" do |vb|
    vb.customize ["modifyvm", :id, "--hwvirtex", "off"]
    vb.cpus = 2
    vb.memory = 2048
  end
  
  config.ssh.username = 'vagrant'
  config.ssh.password = 'vagrant'
  config.ssh.insert_key = 'true'

  config.vm.define "php73", primary: true do |php73|
    php73.vm.box = "bento/ubuntu-18.10"
    php73.vm.network :forwarded_port, guest: 22, host: 2222, id: "ssh"
    php73.vm.provision "shell", path: "vagrant-73.sh"
  end
  
  config.vm.define "php74", autostart: false do |php74|
    php74.vm.box = "bento/ubuntu-18.10"
    php74.vm.network :forwarded_port, guest: 22, host: 2220, id: "ssh"
    php74.vm.provision "shell", path: "vagrant-74.sh"
  end
  
end
