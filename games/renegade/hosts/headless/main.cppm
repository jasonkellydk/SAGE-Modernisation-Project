import std;
import games.renegade.session;
import games.renegade.content.install.install_mount;
import games.renegade.content.armor.armor_catalog;

int main(int argc, char **argv)
{
	renegade::Session session;
	if (argc == 3 && std::string_view(argv[1]) == "--install")
	{
		auto install = renegade::content::MountInstall(std::filesystem::path(argv[2]));
		if (!install) { std::cerr << install.error() << '\n'; return 1; }
		std::cout << "Mounted " << install->archives.size() << " retail archives, " << install->files->FileCount() << " virtual files\n";
		auto armor = renegade::content::LoadArmorCatalog(*install->files);
		if (!armor) { std::cerr << armor.error() << '\n'; return 1; }
		session.World().Resource<renegade::DamageRules>() = std::move(armor->rules);
		std::cout << "Loaded " << armor->armors.size() << " armor types and " << armor->warheads.size() << " warheads\n";
	}
	else if (argc != 1)
	{
		std::cerr << "Usage: renegade_headless [--install <retail directory>]\n";
		return 2;
	}
	session.Step();
	std::cout << "Renegade headless foundation, tick " << session.Tick() << '\n';
	return 0;
}
