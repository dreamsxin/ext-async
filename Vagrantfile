# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|

  config.vm.box = "bento/ubuntu-18.10"

  config.vm.provider "virtualbox" do |vb|
    vb.customize ["modifyvm", :id, "--hwvirtex", "off"]
    vb.cpus = 2
    vb.memory = 2048
  end
  
  config.ssh.username = 'vagrant'
  config.ssh.password = 'vagrant'
  config.ssh.insert_key = 'true'

  config.vm.define "nts" do |box|
    box.vm.network :forwarded_port, guest: 22, host: 2222, id: "ssh"
    box.vm.provision "shell", path: "vagrant-nts.sh"
  end
  
end
