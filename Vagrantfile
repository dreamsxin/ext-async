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

  config.vm.define "ubuntu18", primary: true do |ubuntu18|
    ubuntu18.vm.box = "bento/ubuntu-18.10"
    ubuntu18.vm.network :forwarded_port, guest: 22, host: 2222, id: "ssh"
    ubuntu18.vm.provision "shell", path: "vagrant-nts.sh"
  end
  
  config.vm.define "ubuntu14", autostart: false do |ubuntu14|
    ubuntu14.vm.box = "ubuntu/trusty64"
    ubuntu14.vm.network :forwarded_port, guest: 22, host: 2220, id: "ssh"
    ubuntu14.vm.provision "shell", path: "vagrant-nts.sh"
  end
  
end
